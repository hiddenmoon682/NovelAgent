#pragma once
#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

// 查询单个世界规则。
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

// 列出所有世界规则摘要。
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

// 更新世界规则字段。
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

// 创建新的世界规则。
// 参数: name (string, required), summary/limitations/costs/... (string, optional)
class CreateWorldRuleTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreateWorldRuleTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_world_rule"; }
    std::string description() const override {
        return "创建新的世界规则（魔法体系、科技限制、社会法则等）。"
               "可填写概要、限制条件、代价、例外情况、知晓者等字段。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::WorldRule; }
};

// 删除指定世界规则，并级联清理 Setting 中对该规则 ID 的引用。
class DeleteWorldRuleTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit DeleteWorldRuleTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "delete_world_rule"; }
    std::string description() const override {
        return "删除指定世界规则（魔法体系、社会法则等），自动清理关联 setting 的引用。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::WorldRule; }
};

} // namespace agent
