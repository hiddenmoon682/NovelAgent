#pragma once
#include "agent/tools/BuiltInTool.h"
#include "project/Models.h"
#include <nlohmann/json.hpp>

namespace agent {

/// 查询单个世界规则。
class GetWorldRuleTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetWorldRuleTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_world_rule"; }
    std::string description() const override { return "根据 ID 查询世界规则的详细信息"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::WorldRule; }
};

/// 列出所有世界规则摘要。
class ListWorldRulesTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit ListWorldRulesTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_world_rules"; }
    std::string description() const override { return "列出所有世界规则（ID/名称/概要）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::WorldRule; }
};

/// 更新世界规则字段。
class UpdateWorldRuleTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateWorldRuleTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_world_rule"; }
    std::string description() const override { return "更新指定世界规则的字段（name/summary/limitations/costs 等）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::WorldRule; }
};

} // namespace agent
