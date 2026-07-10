// IToolProvider 实现。

#include "agent/IToolProvider.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace agent {

RestrictedToolProvider::RestrictedToolProvider(
    ToolRegistry& registry, std::vector<std::string> allowed_names)
    : registry_(registry), allowed_(std::move(allowed_names))
{}

std::vector<llm::ToolDefinition> RestrictedToolProvider::getDefinitions() const
{
    if (allowed_.empty()) return {};

    auto allDefs = registry_.getToolDefinitions();
    std::vector<llm::ToolDefinition> filtered;
    filtered.reserve(allowed_.size());  // O(n) 单次遍历

    for (const auto& def : allDefs) {
        if (std::find(allowed_.begin(), allowed_.end(), def.name) != allowed_.end()) {
            filtered.push_back(def);
        }
    }
    return filtered;
}

nlohmann::json RestrictedToolProvider::execute(
    const std::string& name, const nlohmann::json& args)
{
    if (!has(name)) {
        spdlog::warn("[RestrictedToolProvider] 工具 '{}' 不在白名单中", name);
        return {{"error", "工具 '" + name + "' 不在允许列表中"}};
    }
    return registry_.executeTool(name, args);
}

bool RestrictedToolProvider::has(const std::string& name) const
{
    return std::find(allowed_.begin(), allowed_.end(), name) != allowed_.end();
}

} // namespace agent
