#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace skill { class SkillRegistry; }

namespace agent {

// 按需加载技能全文（渐进式披露的第二级）。
// system prompt 只包含 <available_skills> 目录（名称+描述），
// LLM 判断任务匹配某技能时调用本工具获取完整指引。
// 参数: name (string) — 技能名
// 返回: { name, content } 或 { error }
class UseSkillTool : public BuiltInTool {
    skill::SkillRegistry* registry_;
public:
    explicit UseSkillTool(const ToolDependencies& deps)
        : registry_(deps.skill_registry) {}
    std::string name() const override { return "use_skill"; }
    std::string description() const override {
        return "加载指定技能的完整内容。当任务与 <available_skills> 中某技能的描述匹配时，"
               "先调用本工具获取该技能的完整指引，再按指引执行任务。";
    }
    std::string brief() const override { return "按需加载技能全文"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::System; }
    bool isReadOnly() const override { return true; }
};

// 创建或更新技能（供 create-skill 元技能引导用户创建新技能时落盘）。
// 写入 <项目>/skills/<name>/SKILL.md 并刷新注册表，新技能立即可用。
// 参数: name / description / content 必填，always 可选
// 返回: { ok, path } 或 { error }
class SaveSkillTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
    skill::SkillRegistry* registry_;
public:
    explicit SaveSkillTool(const ToolDependencies& deps)
        : project_(deps.project_access), registry_(deps.skill_registry) {}
    std::string name() const override { return "save_skill"; }
    std::string description() const override {
        return "创建或更新一个技能：将 SKILL.md 写入项目 skills/ 目录并立即生效。"
               "name 必须为小写字母数字加连字符（如 dialogue-polish）。"
               "content 为技能正文 Markdown（不含 frontmatter，由工具自动生成）。";
    }
    std::string brief() const override { return "创建或更新技能"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::System; }
};

} // namespace agent
