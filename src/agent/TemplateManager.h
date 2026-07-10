#pragma once

#include "agent/SubAgentTemplate.h"
#include <string>
#include <vector>

namespace agent {

// 模板管理器 — 内置模板 + 用户自定义模板的 CRUD。
class TemplateManager {
public:
    TemplateManager();

    // 获取所有模板（内置 + 用户）。
    const std::vector<SubAgentTemplate>& allTemplates() const { return templates_; }

    // 按名查找。
    const SubAgentTemplate* findTemplate(const std::string& name) const;

    // 添加用户模板（内置模板名不可重复）。
    bool addTemplate(SubAgentTemplate t);

    // 删除用户模板（内置模板不可删除）。
    bool removeTemplate(const std::string& name);

private:
    std::vector<SubAgentTemplate> templates_;
};

} // namespace agent
