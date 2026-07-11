#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

/// 获取与指定章节最相关的世界观规则完整信息。
class GetRelevantWorldRulesTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetRelevantWorldRulesTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_relevant_world_rules"; }
    std::string description() const override {
        return "获取与指定章节最相关的世界观规则完整信息。规则通过本章相关的设定地点反向关联，"
               "确保只返回本章实际需要遵守的规则。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::WorldRule; }
};

} // namespace agent
