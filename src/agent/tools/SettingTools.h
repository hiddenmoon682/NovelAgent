#pragma once
#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

/// 查询单个设定。
class GetSettingTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetSettingTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_setting"; }
    std::string description() const override { return "根据 ID 查询世界观设定的详细信息"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

/// 列出所有设定摘要。
class ListSettingsTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit ListSettingsTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_settings"; }
    std::string description() const override { return "列出所有世界观设定（ID/名称/类别/描述）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

/// 更新设定字段。
class UpdateSettingTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateSettingTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_setting"; }
    std::string description() const override { return "更新指定设定的字段（name/category/description/story_function 等）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

/// 创建新的世界观设定。
/// 参数: name (string, required), category/description/story_function/... (string, optional)
class CreateSettingTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreateSettingTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_setting"; }
    std::string description() const override {
        return "创建新的世界观设定（地点/组织/物品等）。"
               "可填写描述、故事功能、感官描写等字段。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

/// 删除指定设定，并级联清理引用（Chapter/Scene/PlotThread/WorldRule/Setting 中对该 ID 的引用）。
class DeleteSettingTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit DeleteSettingTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "delete_setting"; }
    std::string description() const override {
        return "删除指定世界观设定（地点/组织/物品），自动清理所有引用。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

} // namespace agent
