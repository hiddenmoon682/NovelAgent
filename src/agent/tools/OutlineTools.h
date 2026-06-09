#pragma once
#include "agent/tools/BuiltInTool.h"
#include "project/Models.h"
#include <nlohmann/json.hpp>

namespace agent {

/// 获取大纲信息。
/// 参数: 无 → 返回: { outline: { premise, story_structure, ... } }
class GetOutlineTool : public BuiltInTool {
    Project& project_;
public:
    explicit GetOutlineTool(Project& p) : project_(p) {}
    std::string name() const override { return "get_outline"; }
    std::string description() const override { return "获取当前小说的大纲信息（前提、故事结构、幕摘要等）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

/// 查看项目信息。
class GetProjectStatusTool : public BuiltInTool {
    Project& project_;
public:
    explicit GetProjectStatusTool(Project& p) : project_(p) {}
    std::string name() const override { return "get_project_status"; }
    std::string description() const override { return "获取当前项目的概况（标题/logline/主题/目标读者/内容评级等）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Project; }
};

} // namespace agent
