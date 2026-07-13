// IToolProvider 实现。
// RestrictedToolProvider —— 基于白名单的 IToolProvider 安全包装器。
// 用于 SubAgent 场景：限制子代理只能调用白名单中指定的工具，
// 避免它访问危险或不应触及的工具。

#include "agent/IToolProvider.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace agent {

// 构造函数
// registry      底层的完整工具注册表（拥有全部工具定义和执行能力）
// allowed_names 白名单——仅允许这些名称的工具被调用
RestrictedToolProvider::RestrictedToolProvider(
    ToolRegistry& registry, std::vector<std::string> allowed_names)
    : registry_(registry), allowed_(std::move(allowed_names))
{}

// getDefinitions — 返回白名单内工具的定义列表。
// LLM 看到的工具集因此受限，不会知道不允许的工具的存在。
std::vector<llm::ToolDefinition> RestrictedToolProvider::getDefinitions() const
{
    // 白名单为空 → 不允许任何工具
    if (allowed_.empty()) return {};

    // 从注册表获取全量工具定义，然后按白名单过滤
    auto allDefs = registry_.getToolDefinitions();
    std::vector<llm::ToolDefinition> filtered;
    filtered.reserve(allowed_.size());  // 预分配，避免多次扩容

    for (const auto& def : allDefs) {
        // 线性查找 def.name 是否在白名单中
        if (std::find(allowed_.begin(), allowed_.end(), def.name) != allowed_.end()) {
            filtered.push_back(def);
        }
    }
    return filtered;
}

// execute — 在白名单检查通过后，委托 registry_ 实际执行工具。
// 如果工具名不在白名单中，返回错误 JSON 而非抛异常（LLM 友好）。
nlohmann::json RestrictedToolProvider::execute(
    const std::string& name, const nlohmann::json& args)
{
    // 安全检查：拒绝白名单外的工具调用
    if (!has(name)) {
        spdlog::warn("[RestrictedToolProvider] 工具 '{}' 不在白名单中", name);
        return {{"error", "工具 '" + name + "' 不在允许列表中"}};
    }
    // 放行：委托给真实的注册表执行
    return registry_.executeTool(name, args);
}

// has — 检查工具 name 是否在白名单内。
bool RestrictedToolProvider::has(const std::string& name) const
{
    return std::find(allowed_.begin(), allowed_.end(), name) != allowed_.end();
}

} // namespace agent
