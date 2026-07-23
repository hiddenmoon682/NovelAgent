#include "agent/tool/ProgressiveToolProvider.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

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

ProgressiveToolProvider::ProgressiveToolProvider(ToolRegistry& registry)
    : registry_(registry)
{
    initCoreTools();
}

void ProgressiveToolProvider::initCoreTools() {
    std::unique_lock lock(mutex_);
    loaded_tools_.clear();
    loaded_tools_.insert("tool_search");
    for (const auto& name : kCoreTools) {
        if (registry_.hasTool(name))
            loaded_tools_.insert(name);
    }
}

std::vector<llm::ToolDefinition> ProgressiveToolProvider::getDefinitions() const {
    if (!enabled_)
        return registry_.getToolDefinitions();

    std::shared_lock lock(mutex_);
    std::vector<llm::ToolDefinition> defs;
    defs.reserve(loaded_tools_.size());

    defs.push_back(toolSearchDefinition());

    auto all = registry_.getToolDefinitions();
    for (auto& def : all) {
        if (loaded_tools_.count(def.name))
            defs.push_back(std::move(def));
    }
    return defs;
}

nlohmann::json ProgressiveToolProvider::execute(
    const std::string& name, const nlohmann::json& args)
{
    if (name == "tool_search")
        return executeToolSearch(args);

    if (!enabled_)
        return registry_.executeTool(name, args);

    {
        std::shared_lock lock(mutex_);
        if (!loaded_tools_.count(name)) {
            if (registry_.hasTool(name)) {
                return {
                    {"error", "工具 '" + name + "' 尚未加载到当前工具列表"},
                    {"suggestion", "请先调用 tool_search(query=\"" + name + "\") 加载此工具的完整定义"},
                    {"retryable", true}
                };
            }
            return {{"error", "工具 '" + name + "' 不存在"}};
        }
    }

    return registry_.executeTool(name, args);
}

bool ProgressiveToolProvider::has(const std::string& name) const {
    if (name == "tool_search") return true;
    if (!enabled_) return registry_.hasTool(name);
    std::shared_lock lock(mutex_);
    return loaded_tools_.count(name) > 0;
}

std::vector<std::string> ProgressiveToolProvider::toolNamesByCategory(
    ToolCategory category) const
{
    return registry_.toolNamesByCategory(category);
}

size_t ProgressiveToolProvider::loadedCount() const {
    std::shared_lock lock(mutex_);
    return loaded_tools_.size();
}

nlohmann::json ProgressiveToolProvider::executeToolSearch(const nlohmann::json& args) {
    std::string query = args.value("query", "");
    if (query.empty())
        return {{"error", "query 参数不能为空"}};

    nlohmann::json results = nlohmann::json::array();

    std::unique_lock lock(mutex_);

    // 精确模式: "select:get_character"
    if (query.size() > 7 && query.substr(0, 7) == "select:") {
        std::string name = query.substr(7);
        auto def = registry_.getToolDefinition(name);
        if (def) {
            loaded_tools_.insert(name);
            results.push_back({
                {"name", def->name},
                {"description", def->description},
                {"parameters", def->parameters}
            });
        }
    } else {
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

std::string ProgressiveToolProvider::deferredToolsStub() const {
    if (!enabled_) return "";

    auto summaries = registry_.getToolSummaries();

    std::shared_lock lock(mutex_);

    bool has_deferred = false;
    for (const auto& s : summaries) {
        if (!loaded_tools_.count(s.name)) {
            has_deferred = true;
            break;
        }
    }
    if (!has_deferred) return "";

    std::string stub;
    stub += "\n\n<available-deferred-tools>\n";
    stub += "以下工具可通过 tool_search 加载后使用（当前未在工具列表中）：\n";
    for (const auto& s : summaries) {
        if (!loaded_tools_.count(s.name)) {
            stub += "- " + s.name + ": " + s.description + "\n";
        }
    }
    stub += "\n工具加载规则（必须遵守）：\n";
    stub += "1. 上述工具当前不可直接调用，必须先通过 tool_search 加载\n";
    stub += "2. 调用 tool_search(query=\"关键词\") 搜索，或 tool_search(query=\"select:工具名\") 精确加载\n";
    stub += "3. 加载后工具将在后续轮次自动出现在可用工具列表中\n";
    stub += "4. 禁止猜测未加载工具的参数格式，必须先加载查看完整 schema\n";
    stub += "</available-deferred-tools>";
    return stub;
}

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

void ProgressiveToolProvider::reset() {
    initCoreTools();
}

} // namespace agent
