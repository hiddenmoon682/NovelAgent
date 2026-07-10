#pragma once
#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

// 获取大纲信息。
// 参数: 无 → 返回: { outline: { premise, story_structure, ... } }
class GetOutlineTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetOutlineTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_outline"; }
    std::string description() const override { return "获取当前小说的大纲信息（前提、故事结构、幕摘要等）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

// 查看项目信息。
class GetProjectStatusTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetProjectStatusTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_project_status"; }
    std::string description() const override { return "获取当前项目的概况（标题/logline/主题/目标读者/内容评级等）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Project; }
};

// 创建新卷。
class CreateVolumeTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreateVolumeTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_volume"; }
    std::string description() const override {
        return "创建新的卷（故事叙事弧线）。可指定标题、主题、目标、起始/结束章节等。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

// 更新卷字段。
class UpdateVolumeTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateVolumeTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_volume"; }
    std::string description() const override {
        return "更新指定卷的字段（title/summary/theme/goal/order/key_events 等）。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

// 创建新剧情线。
class CreatePlotThreadTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreatePlotThreadTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_plot_thread"; }
    std::string description() const override {
        return "创建新的剧情线（主线/支线/伏笔）。可指定类型、优先级、风险、中心问题等。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

// 更新剧情线字段。
class UpdatePlotThreadTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdatePlotThreadTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_plot_thread"; }
    std::string description() const override {
        return "更新指定剧情线的字段（name/description/type/status/priority 等）。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

} // namespace agent
