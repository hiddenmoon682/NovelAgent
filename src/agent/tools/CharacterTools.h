#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

// 查询单个角色完整档案。
// 参数: character_id (string)
// 返回: { id, name, role, ...全字段 }
class GetCharacterTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit GetCharacterTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "get_character"; }
    std::string description() const override {
        return "根据角色 ID 查询完整档案，包括性格、背景、目标、关系等所有字段";
    }
    std::string brief() const override { return "查询单个角色完整档案"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
    bool isReadOnly() const override { return true; }
};

// 列出所有角色摘要。
// 参数: 无
// 返回: { characters: [{ id, name, role, goal }] }
class ListCharactersTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit ListCharactersTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "get_characters"; }
    std::string description() const override {
        return "列出当前项目所有角色的 ID、姓名、定位和当前目标";
    }
    std::string brief() const override { return "列出所有角色摘要"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
    bool isReadOnly() const override { return true; }
};

// 创建新角色。
// 支持在创建时填充性格/背景/目标等叙事字段，减少后续手动编辑。
// 参数: name (string, required), role/personality/background/goal/... (string, optional)
// 返回: { success, character: { id, name, role } }
class CreateCharacterTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit CreateCharacterTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "create_character"; }
    std::string description() const override {
        return "创建新角色：指定姓名和定位（protagonist/antagonist/supporting/minor），"
               "可同时填写性格、背景、目标、动机等叙事字段。自动生成 ID 并保存。";
    }
    std::string brief() const override { return "创建新角色"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

// 更新角色字段。
// 参数: character_id (string), fields (object — 任意字段名→值)
// 返回: { success, character: { id, name, ...已更新字段 } }
class UpdateCharacterTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit UpdateCharacterTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "update_character"; }
    std::string description() const override {
        return "更新指定角色的字段（可更新任意字段：personality/background/goal/motivation 等），只修改传入的字段";
    }
    std::string brief() const override { return "更新角色字段"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

// 删除指定角色，并级联清理其他角色 Relationships / Setting / PlotThread / Volume / Chapter / Scene 中对该角色的引用。
// 参数: character_id (string, required)
// 返回: { success, deleted_id, cascade: { ... } }
class DeleteCharacterTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit DeleteCharacterTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "delete_character"; }
    std::string description() const override {
        return "删除指定角色，自动清理所有角色关系网和引用。";
    }
    std::string brief() const override { return "删除角色并清理引用"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

// 完整替换指定角色的人际关系列表。
// 参数: character_id (string, required), relationships (array of relationship objects, required)
// 返回: { success, character_id, relationship_count }。输入中若有元素不是对象：写入合法项并
//       额外返回 skipped 与 warning；若全部元素都不合法则拒绝执行（避免清空原有关系），
//       返回 { error, skipped }。传空数组是明确的清空意图，照常执行。
class UpdateCharacterRelationshipsTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit UpdateCharacterRelationshipsTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "update_character_relationships"; }
    std::string description() const override {
        return "完整替换指定角色的人际关系列表（每项含 target_character_id/type/description/tension 等字段）。";
    }
    std::string brief() const override { return "替换角色人际关系列表"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

// 按目标角色 ID 局部修改某角色的一条关系（只改传入的字段，同一角色的其它关系原样不动）。
// 参数: character_id (string, required), target_character_id (string, required), fields (object, required)
// 返回: { success, character_id, target_character_id, updated_fields }
// 与 update_character_relationships（整体替换）的分工：小改动用本工具，增删/重排整表才用那个。
// 关系没有自己的 id，身份即 target_character_id；它同时是定位键——改指向对象等于「删一条 + 加一条」，
// 只能走整体替换工具，故不在可改字段里。
class UpdateRelationshipTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit UpdateRelationshipTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "update_relationship"; }
    std::string description() const override {
        return "按目标角色 ID 局部修改某角色的一条关系（改 type/description/tension 等字段）。"
               "只动这一条，同角色其它关系不变；改动单条关系时优先用它，不要用 "
               "update_character_relationships 重传整表。";
    }
    std::string brief() const override { return "局部修改单条人际关系"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

// 为角色添加发展记录（弧光追踪）。
class AddCharacterDevelopmentTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit AddCharacterDevelopmentTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "add_character_development"; }
    std::string description() const override {
        return "为指定角色添加一条发展记录，记录该角色在特定章节中的变化。";
    }
    std::string brief() const override { return "添加角色发展记录（弧光追踪）"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Character; }
};

} // namespace agent
