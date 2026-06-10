#pragma once

#include "agent/tools/BuiltInTool.h"
#include "project/Models.h"

#include <nlohmann/json.hpp>

namespace agent {

/// 查询单个角色完整档案。
/// 参数: character_id (string)
/// 返回: { id, name, role, ...全字段 }
class GetCharacterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetCharacterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_character"; }
    std::string description() const override {
        return "根据角色 ID 查询完整档案，包括性格、背景、目标、关系等所有字段";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

/// 列出所有角色摘要。
/// 参数: 无
/// 返回: { characters: [{ id, name, role, goal }] }
class ListCharactersTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit ListCharactersTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_characters"; }
    std::string description() const override {
        return "列出当前项目所有角色的 ID、姓名、定位和当前目标";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

/// 创建新角色。
/// 参数: name (string), role? (string, 默认 "supporting")
/// 返回: { success, character: { id, name, role } }
class CreateCharacterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreateCharacterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_character"; }
    std::string description() const override {
        return "创建新角色：指定姓名和定位（protagonist/antagonist/supporting/minor），自动生成 ID 并保存";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

/// 更新角色字段。
/// 参数: character_id (string), fields (object — 任意字段名→值)
/// 返回: { success, character: { id, name, ...已更新字段 } }
class UpdateCharacterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateCharacterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_character"; }
    std::string description() const override {
        return "更新指定角色的字段（可更新任意字段：personality/background/goal/motivation 等），只修改传入的字段";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

} // namespace agent
