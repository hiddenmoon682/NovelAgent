#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

/// 获取与指定章节最相关的角色列表及其完整档案。
/// 角色按关联系数从高到低排序：POV > 焦点 > 场景参与者 > 剧情线关联 > 本章有发展 > 本章有出场。
class GetRelevantCharactersTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetRelevantCharactersTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_relevant_characters"; }
    std::string description() const override {
        return "获取与指定章节最相关的角色列表及其完整档案。角色按与该章节的关联系数从高到低排序。"
               "在写作前调用此工具了解本章需要关注的角色。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

} // namespace agent
