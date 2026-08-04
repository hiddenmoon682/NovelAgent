#include "agent/tool/ToolRegistry.h"

#include <algorithm>
#include <stdexcept>

namespace agent {

// ===========================================================================
// 注册
// ===========================================================================

// 注册函数式工具（轻量、无状态）。
// 将工具信息包装为 ToolEntry 存入内部容器 tools_。
// parameters 在此处拷贝一次，构造函数签名要求 const&，注册阶段非热路径故可接受。
void ToolRegistry::registerTool(
    std::string name,
    std::string description,
    const nlohmann::json& parameters,
    ToolCategory category,
    std::function<nlohmann::json(const nlohmann::json&)> fn,
    std::string brief,
    bool is_readonly)
{
    if (brief.empty()) brief = description;  // 空则回退 description
    tools_.push_back({
        std::move(name),
        std::move(description),
        std::move(brief),
        parameters,  // 拷贝一次（注册阶段非热路径）
        category,
        std::move(fn),
        is_readonly
    });
}

// 注册类式工具实例（复杂、有状态）。
// 持有 BuiltInTool 的所有权（存入 builtin_instances_），
// 捕获裸指针包装为函数式回调后委托 registerTool 统一存储。
void ToolRegistry::registerBuiltInTool(std::unique_ptr<BuiltInTool> tool)
{
    // 捕获裸指针用于回调（所有权由 builtin_instances_ 持有）
    auto* raw = tool.get();
    builtin_instances_.push_back(std::move(tool));

    registerTool(
        raw->name(),
        raw->description(),
        raw->parameters(),
        raw->category(),
        [raw](const nlohmann::json& args) {
            return raw->execute(args);
        },
        raw->brief(),
        raw->isReadOnly()
    );
}

bool ToolRegistry::isReadOnly(const std::string& name) const {
    const ToolEntry* e = findTool(name);
    if (e && e->is_readonly) return true;   // 显式标记只读优先
    return IToolProvider::defaultIsReadOnly(name);  // 否则前缀启发式（保留只读工具共享锁）
}

// ===========================================================================
// 查询
// ===========================================================================

// 获取所有已注册工具的定义列表（全量，不区分加载状态）。
// 返回的 ToolDefinition 结构包含 name + description + parameters，
// 可直接作为 LLM API 的 tools 参数使用。
std::vector<llm::ToolDefinition> ToolRegistry::getToolDefinitions() const
{
    std::vector<llm::ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& entry : tools_) {
        defs.push_back({
            entry.name,
            entry.description,
            entry.parameters
        });
    }
    return defs;
}

// 检查指定名称的工具是否已注册（精确比对 name）。
bool ToolRegistry::hasTool(const std::string& name) const
{
    return findTool(name) != nullptr;
}

// ===========================================================================
// 执行
// ===========================================================================

// 执行指定工具。
// name  工具名称（必须在 tools_ 中已注册）
// args  调用参数（LLM 返回的 arguments JSON）
// 返回值：
//   工具执行结果 JSON（正常返回）
//   或 {"error": "..."}（工具不存在 / 执行抛异常）
nlohmann::json ToolRegistry::executeTool(const std::string& name,
                                          const nlohmann::json& args)
{
    const auto* tool = findTool(name);
    if (!tool) {
        // 未找到工具时返回错误 + 完整可用工具列表，帮助 LLM 自我修正
        nlohmann::json available = nlohmann::json::array();
        for (const auto& t : tools_) {
            available.push_back(t.name);
        }
        return {
            {"error", "工具 '" + name + "' 不存在"},
            {"available_tools", std::move(available)}
        };
    }

    try {
        return tool->fn(args);
    } catch (const std::exception& e) {
        return {
            {"error", std::string("工具 '") + name + "' 执行异常: " + e.what()}
        };
    } catch (...) {
        return {
            {"error", std::string("工具 '") + name + "' 执行异常: 未知错误"}
        };
    }
}

// ===========================================================================
// 分组查询
// ===========================================================================

// 获取所有工具名称列表（无过滤，用于 /help 等命令）。
std::vector<std::string> ToolRegistry::toolNames() const
{
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& t : tools_) {
        names.push_back(t.name);
    }
    return names;
}

// 按类别获取工具名称列表。
std::vector<std::string> ToolRegistry::toolNamesByCategory(
    ToolCategory category) const
{
    std::vector<std::string> names;
    for (const auto& t : tools_) {
        if (t.category == category) {
            names.push_back(t.name);
        }
    }
    return names;
}

// ===========================================================================
// 渐进式加载查询
// ===========================================================================

// 按名称精确查找单个工具定义（渐进式加载用）。
// 未找到时返回 nullopt，由调用方（ProgressiveToolProvider::executeToolSearch）
// 决定如何处理——精确模式返回空结果，通知 LLM 未找到。
std::optional<llm::ToolDefinition> ToolRegistry::getToolDefinition(
    const std::string& name) const
{
    const auto* entry = findTool(name);
    if (!entry) return std::nullopt;
    return llm::ToolDefinition{entry->name, entry->description, entry->parameters};
}

// 按关键词搜索工具（名称或描述包含 query）。
// 使用 std::string::find 子串匹配，区分大小写。
// 渐进式加载中 tool_search 的关键词模式使用此接口查找匹配工具。
std::vector<llm::ToolDefinition> ToolRegistry::searchTools(
    const std::string& query) const
{
    std::vector<llm::ToolDefinition> results;
    for (const auto& t : tools_) {
        if (t.name.find(query) != std::string::npos ||
            t.description.find(query) != std::string::npos) {
            results.push_back({t.name, t.description, t.parameters});
        }
    }
    return results;
}

// 获取所有工具的摘要信息（名称 + 描述 + 类别）。
// 不含 parameters（不暴露完整 schema），用于：
// - ProgressiveToolProvider::deferredToolsStub() 生成存根文本
// - tool_search 未匹配时返回所有延迟工具名列表，引导 LLM 重新搜索
std::vector<ToolRegistry::ToolSummary> ToolRegistry::getToolSummaries() const
{
    std::vector<ToolSummary> summaries;
    summaries.reserve(tools_.size());
    for (const auto& t : tools_) {
        summaries.push_back({t.name, t.description, t.brief, t.category});
    }
    return summaries;
}

// ===========================================================================
// 内部辅助
// ===========================================================================

// 按名称查找工具条目，返回指针；未找到返回 nullptr。
const ToolRegistry::ToolEntry* ToolRegistry::findTool(
    const std::string& name) const
{
    auto it = std::find_if(tools_.begin(), tools_.end(),
        [&name](const ToolEntry& entry) { return entry.name == name; });
    return (it != tools_.end()) ? &(*it) : nullptr;
}

} // namespace agent
