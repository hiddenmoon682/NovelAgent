#pragma once
#include "agent/tools/BuiltInTool.h"
#include "project/Models.h"
#include <nlohmann/json.hpp>

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

} // namespace agent
