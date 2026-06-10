#include "agent/ToolRegistry.h"

#include <algorithm>
#include <stdexcept>

namespace agent {

// ===========================================================================
// 注册
// ===========================================================================

void ToolRegistry::registerTool(
    std::string name,
    std::string description,
    const nlohmann::json& parameters,
    ToolCategory category,
    std::function<nlohmann::json(const nlohmann::json&)> fn)
{
    tools_.push_back({
        std::move(name),
        std::move(description),
        parameters,  // 拷贝一次（注册阶段非热路径）
        category,
        std::move(fn)
    });
}

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
        }
    );
}

// ===========================================================================
// 查询
// ===========================================================================

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

bool ToolRegistry::hasTool(const std::string& name) const
{
    return findTool(name) != nullptr;
}

// ===========================================================================
// 执行
// ===========================================================================

nlohmann::json ToolRegistry::executeTool(const std::string& name,
                                          const nlohmann::json& args)
{
    const auto* tool = findTool(name);
    if (!tool) {
        // 收集可用工具名列表，帮助 LLM 自我修正
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

std::vector<std::string> ToolRegistry::toolNames() const
{
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& t : tools_) {
        names.push_back(t.name);
    }
    return names;
}

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
// 内部辅助
// ===========================================================================

const ToolRegistry::ToolEntry* ToolRegistry::findTool(
    const std::string& name) const
{
    auto it = std::find_if(tools_.begin(), tools_.end(),
        [&name](const ToolEntry& entry) { return entry.name == name; });
    return (it != tools_.end()) ? &(*it) : nullptr;
}

} // namespace agent
