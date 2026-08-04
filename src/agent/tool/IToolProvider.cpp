// IToolProvider 实现。
// RestrictedToolProvider —— 基于白名单的 IToolProvider 安全包装器。

#include "agent/tool/IToolProvider.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <string_view>
#include <nlohmann/json.hpp>

namespace agent {

bool IToolProvider::defaultIsReadOnly(const std::string& name) {
    static constexpr std::array kPrefixes = {
        std::string_view{"get_"},
        std::string_view{"list_"},
        std::string_view{"read_"},
        std::string_view{"search_"},
    };
    for (auto prefix : kPrefixes) {
        if (name.starts_with(prefix))
            return true;
    }
    return false;
}

bool IToolProvider::isReadOnly(const std::string& name) const {
    return defaultIsReadOnly(name);
}

RestrictedToolProvider::RestrictedToolProvider(
    IToolProvider& parent, std::vector<std::string> allowed_names)
    : parent_(parent), allowed_(std::move(allowed_names))
{}

std::vector<llm::ToolDefinition> RestrictedToolProvider::getDefinitions() const
{
    if (allowed_.empty()) return {};

    auto allDefs = parent_.getDefinitions();
    std::vector<llm::ToolDefinition> filtered;
    filtered.reserve(allowed_.size());

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
    return parent_.execute(name, args);
}

bool RestrictedToolProvider::has(const std::string& name) const
{
    return std::find(allowed_.begin(), allowed_.end(), name) != allowed_.end();
}

std::vector<std::string> RestrictedToolProvider::toolNamesByCategory(ToolCategory category) const
{
    auto names = parent_.toolNamesByCategory(category);
    std::vector<std::string> filtered;
    for (auto& n : names) {
        if (has(n)) filtered.push_back(std::move(n));
    }
    return filtered;
}

bool RestrictedToolProvider::isReadOnly(const std::string& name) const {
    return parent_.isReadOnly(name);
}

} // namespace agent
