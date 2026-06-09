#include "agent/TemplateManager.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {

TemplateManager::TemplateManager() {
    // 加载内置模板
    templates_ = builtInTemplates();
}

const SubAgentTemplate* TemplateManager::findTemplate(
    const std::string& name) const
{
    auto it = std::find_if(templates_.begin(), templates_.end(),
        [&](const SubAgentTemplate& t) { return t.name == name; });
    return (it != templates_.end()) ? &(*it) : nullptr;
}

bool TemplateManager::addTemplate(SubAgentTemplate t)
{
    // 检查重名
    if (findTemplate(t.name)) {
        spdlog::warn("[TemplateManager] 模板 '{}' 已存在", t.name);
        return false;
    }
    t.built_in = false; // 用户模板不是内置的
    templates_.push_back(std::move(t));
    spdlog::info("[TemplateManager] 添加模板: {}", templates_.back().name);
    return true;
}

bool TemplateManager::removeTemplate(const std::string& name)
{
    auto it = std::find_if(templates_.begin(), templates_.end(),
        [&](const SubAgentTemplate& t) { return t.name == name; });
    if (it == templates_.end()) return false;
    if (it->built_in) {
        spdlog::warn("[TemplateManager] 不能删除内置模板 '{}'", name);
        return false;
    }
    templates_.erase(it);
    spdlog::info("[TemplateManager] 删除模板: {}", name);
    return true;
}

} // namespace agent
