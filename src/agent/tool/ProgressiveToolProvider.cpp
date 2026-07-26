#include "agent/tool/ProgressiveToolProvider.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

// ============================================================
// 核心工具列表 — 这些工具在启动时自动加载到 LLM 工具列表中，
// 不需要通过 tool_search 搜索即可直接调用。
// 它们是 Agent 最基本的功能：读写章节、查看大纲、查询项目状态等。
// ============================================================
namespace {

const std::vector<std::string> kCoreTools = {
    "read_chapter",
    "write_chapter",
    "append_to_chapter",
    "list_chapters",
    "get_latest_chapter",
    "get_outline",
    "get_project_status",
    "get_chapter_context",
};

} // namespace

// ------------------------------------------------------------
// 构造函数：保存注册表引用后初始化核心工具集
// ------------------------------------------------------------
ProgressiveToolProvider::ProgressiveToolProvider(ToolRegistry& registry)
    : registry_(registry)
{
    initCoreTools();
}

// ------------------------------------------------------------
// 初始化核心工具集
// 1. 清空已加载工具集合
// 2. 强制加载 tool_search 本身（这是加载其他工具的入口）
// 3. 从注册表中加载 kCoreTools 中定义的所有核心工具
// 注意：持有互斥锁（写锁），确保线程安全
// ------------------------------------------------------------
void ProgressiveToolProvider::initCoreTools() {
    std::unique_lock lock(mutex_);       // 写操作需要 unique_lock
    loaded_tools_.clear();
    loaded_tools_.insert("tool_search"); // tool_search 自身始终可用
    for (const auto& name : kCoreTools) {
        if (registry_.hasTool(name))
            loaded_tools_.insert(name);
    }
}

// ------------------------------------------------------------
// 获取 LLM 可用的工具定义列表
//
// 两种模式：
// - 未启用渐进式（enabled_ = false）：返回注册表中所有工具（全量模式）
// - 启用渐进式（enabled_ = true）：只返回已加载的工具 + tool_search
//
// 使用 shared_lock（读锁），允许多个线程同时调用此方法。
// ------------------------------------------------------------
std::vector<llm::ToolDefinition> ProgressiveToolProvider::getDefinitions() const {
    // 未启用 → 直接返回全部工具定义，行为等同于普通 ToolProvider
    if (!enabled_)
        return registry_.getToolDefinitions();

    // 已启用 → 只返回已加载的工具
    std::shared_lock lock(mutex_);        // 读操作使用 shared_lock，允许多线程并发读
    std::vector<llm::ToolDefinition> defs;
    defs.reserve(loaded_tools_.size());

    // 始终把 tool_search 放在第一个位置，便于 LLM 发现
    defs.push_back(toolSearchDefinition());

    // 遍历注册表中的所有工具定义，只保留已加载的
    auto all = registry_.getToolDefinitions();
    for (auto& def : all) {
        if (loaded_tools_.count(def.name))
            defs.push_back(std::move(def));
    }
    return defs;
}

// ------------------------------------------------------------
// 执行工具调用
//
// 核心逻辑三层拦截（从外层到内层）：
//   1. tool_search 自身 → 直接路由到 executeToolSearch
//   2. 未启用渐进式 → 透传到注册表（全量模式）
//   3. 已启用但工具未加载 → 返回引导错误，指导 LLM 先调用 tool_search
//
// 这种设计实现了"API 层面硬约束"：未加载的工具不会被真正调用，
// 从而迫使 LLM 按照渐进式流程与系统交互。
// ------------------------------------------------------------
nlohmann::json ProgressiveToolProvider::execute(
    const std::string& name, const nlohmann::json& args)
{
    // 第一层：拦截 tool_search 自身，不经过加载检查
    if (name == "tool_search")
        return executeToolSearch(args);

    // 第二层：未启用时直接透传，行为等同于普通 ToolProvider
    if (!enabled_)
        return registry_.executeTool(name, args);

    // 第三层：检查工具是否已加载
    {
        std::shared_lock lock(mutex_);  // 读锁，允许并发检查
        if (!loaded_tools_.count(name)) {
            // 工具在注册表中存在但未加载 → 返回引导性错误
            if (registry_.hasTool(name)) {
                return {
                    {"error", "工具 '" + name + "' 尚未加载到当前工具列表"},
                    {"suggestion", "请先调用 tool_search(query=\"" + name + "\") 加载此工具的完整定义"},
                    {"retryable", true}   // LLM 可据此判断该错误可重试
                };
            }
            // 工具根本不存在
            return {{"error", "工具 '" + name + "' 不存在"}};
        }
    }

    // 检查通过，委托注册表实际执行
    return registry_.executeTool(name, args);
}

// ------------------------------------------------------------
// 检查工具是否存在（及是否已加载）
//
// - tool_search 始终存在
// - 未启用时查看注册表（全部工具可见）
// - 已启用时只检查 loaded_tools_（未加载的工具视为不存在）
// ------------------------------------------------------------
bool ProgressiveToolProvider::has(const std::string& name) const {
    if (name == "tool_search") return true;
    if (!enabled_) return registry_.hasTool(name);
    std::shared_lock lock(mutex_);
    return loaded_tools_.count(name) > 0;
}

// ------------------------------------------------------------
// 按类别获取工具名称列表 — 直接委托给注册表
// ------------------------------------------------------------
std::vector<std::string> ProgressiveToolProvider::toolNamesByCategory(
    ToolCategory category) const
{
    return registry_.toolNamesByCategory(category);
}

// ------------------------------------------------------------
// 获取当前已加载的工具数量（用于监控/日志）
// ------------------------------------------------------------
size_t ProgressiveToolProvider::loadedCount() const {
    std::shared_lock lock(mutex_);
    return loaded_tools_.size();
}

// ------------------------------------------------------------
// 执行 tool_search — 搜索并加载工具
//
// 两种搜索模式：
// 1. 精确模式（select:工具名）：通过 "select:get_character" 格式精确加载某个工具
// 2. 关键词模式：通过关键词搜索（匹配工具名和描述），加载所有匹配的工具
//
// 工作流程：
// 1. 搜索匹配的工具
// 2. 将匹配的工具加入 loaded_tools_ 集合
// 3. 返回工具的完整 schema（含参数定义），供 LLM 后续轮次使用
//
// 线程安全：此方法持有 unique_lock（写锁），与 getDefinitions 的读锁互斥。
// ------------------------------------------------------------
nlohmann::json ProgressiveToolProvider::executeToolSearch(const nlohmann::json& args) {
    std::string query = args.value("query", "");
    if (query.empty())
        return {{"error", "query 参数不能为空"}};

    nlohmann::json results = nlohmann::json::array();

    std::unique_lock lock(mutex_);  // 写操作，需要互斥

    // === 精确模式: "select:get_character" ===
    // 适用于 LLM 明确知道工具名的情况，跳过搜索直接加载
    if (query.size() > 7 && query.substr(0, 7) == "select:") {
        std::string name = query.substr(7);
        auto def = registry_.getToolDefinition(name);
        if (def) {
            loaded_tools_.insert(name);
            results.push_back({
                {"name", def->name},
                {"description", def->description},
                {"parameters", def->parameters}  // 返回完整 JSON Schema
            });
        }
    }
    // === 关键词模式 ===
    // 通过注册表的 searchTools 接口模糊搜索工具名和描述
    else {
        auto matches = registry_.searchTools(query);
        for (auto& def : matches) {
            loaded_tools_.insert(def.name);
            results.push_back({
                {"name", def.name},
                {"description", def.description},
                {"parameters", def.parameters}
            });
        }
    }

    // 未找到匹配 → 返回所有未加载工具的列表，引导 LLM 重新搜索
    if (results.empty()) {
        auto summaries = registry_.getToolSummaries();
        nlohmann::json available = nlohmann::json::array();
        for (const auto& s : summaries) {
            if (!loaded_tools_.count(s.name))
                available.push_back(s.name);
        }
        return {
            {"message", "未找到匹配 '" + query + "' 的工具"},
            {"available_deferred_tools", std::move(available)}
        };
    }

    spdlog::info("[ProgressiveToolProvider] tool_search('{}') 加载了 {} 个工具",
                 query, results.size());
    return {
        {"loaded_tools", std::move(results)},
        {"hint", "以上工具已加载，后续轮次可直接调用"}
    };
}

// ------------------------------------------------------------
// 生成"延迟工具存根"文本 — 注入到 System Prompt 中
//
// 这是渐进式加载的第二层约束："system prompt 规则"。
// 列出所有非核心工具（核心工具始终直接加载，无需搜索），
// 告知 LLM 有哪些工具可通过 tool_search 加载。
//
// 输出是静态的：只依赖注册表内容，不随 loaded_tools_ 变化，
// 从而保证 system prompt 在会话期间稳定，最大化 KV cache 命中率。
// 使用 brief（极简描述）而非完整 description，减小存根体积。
// 如果渐进式功能未启用，返回空字符串。
// ------------------------------------------------------------
std::string ProgressiveToolProvider::deferredToolsStub() const {
    if (!enabled_) return "";

    // 核心工具 + tool_search 始终直接可用，不进存根
    static const std::set<std::string> excluded = [] {
        std::set<std::string> s(kCoreTools.begin(), kCoreTools.end());
        s.insert("tool_search");
        return s;
    }();

    auto summaries = registry_.getToolSummaries();

    std::string stub;
    stub += "\n\n## 可用延迟工具\n\n";
    stub += "以下工具可通过 tool_search 加载后使用（当前未在工具列表中）：\n\n";
    int count = 0;
    for (const auto& s : summaries) {
        if (!excluded.count(s.name)) {
            stub += "- " + s.name + ": " + s.brief + "\n";
            ++count;
        }
    }
    if (count == 0) return "";

    stub += "\n**工具加载规则：**\n";
    stub += "1. 上述工具当前不可直接调用，需要先通过 tool_search 加载\n";
    stub += "2. 调用 tool_search(query=\"关键词\") 搜索，或 tool_search(query=\"select:工具名\") 精确加载\n";
    stub += "3. 加载后工具将在后续轮次自动出现在可用工具列表中\n";
    stub += "4. 禁止猜测未加载工具的参数格式，必须先加载查看完整 schema\n";
    return stub;
}

// ------------------------------------------------------------
// 定义 tool_search 工具的 schema（静态方法）
//
// 返回 ToolDefinition 结构，包含：
// - 工具名：tool_search
// - 描述：中文本地化说明，告知 LLM 使用场景和两种搜索模式
// - 参数：JSON Schema 格式，只有一个 query 字符串参数
//
// 这个定义会被插入到 getDefinitions() 的返回列表首位，
// 确保 LLM 始终能发现这个"工具发现工具"。
// ------------------------------------------------------------
llm::ToolDefinition ProgressiveToolProvider::toolSearchDefinition() {
    return {
        "tool_search",
        "搜索并加载工具的完整定义。当你需要使用某个功能但当前工具列表中没有对应工具时，"
        "用此工具搜索并加载。支持关键词搜索（如 \"character\"、\"角色\"、\"setting\"）"
        "和精确加载（如 \"select:get_character\"）。加载后工具将在后续轮次可用。",
        {
            {"type", "object"},
            {"properties", {
                {"query", {
                    {"type", "string"},
                    {"description", "搜索关键词（匹配工具名和描述）或 \"select:工具名\" 精确加载"}
                }}
            }},
            {"required", nlohmann::json::array({"query"})}
        }
    };
}

// ------------------------------------------------------------
// reset — 重置加载状态到初始值
// 新会话开始时调用，清空动态加载的工具，只保留核心工具集
// ------------------------------------------------------------
void ProgressiveToolProvider::reset() {
    initCoreTools();
}

} // namespace agent
