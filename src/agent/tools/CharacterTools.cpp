#include "agent/tools/CharacterTools.h"

#include "project/ProjectIO.h"
#include "utils/SchemaUtils.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {

using json = nlohmann::json;

// ===========================================================================
// 辅助
// ===========================================================================

namespace {

/// 按 ID 查找角色
Character* findCharacter(std::vector<Character>& chars, const std::string& id) {
    auto it = std::find_if(chars.begin(), chars.end(),
        [&](const Character& c) { return c.id == id; });
    return (it != chars.end()) ? &(*it) : nullptr;
}

} // namespace

// ===========================================================================
// GetCharacterTool
// ===========================================================================

json GetCharacterTool::parameters() const {
    return utils::schema::object({
        {"character_id", utils::schema::stringProp("角色 ID，例如 char-001")}
    }, {"character_id"});
}

json GetCharacterTool::execute(const json& args) {
    std::string id = args.value("character_id", "");
    const auto* ch = findCharacter(project_->characters, id);
    if (!ch) {
        return {{"error", "角色 '" + id + "' 不存在"}};
    }

    spdlog::info("[get_character] {}", id);
    return json(*ch); // 利用 Models.h 中的 to_json(Character)
}

// ===========================================================================
// ListCharactersTool
// ===========================================================================

json ListCharactersTool::parameters() const {
    return utils::schema::object({});
}

json ListCharactersTool::execute(const json& /*args*/) {
    json chars = json::array();
    for (const auto& ch : project_->characters) {
        chars.push_back({
            {"id", ch.id},
            {"name", ch.name},
            {"role", ch.role},
            {"goal", ch.goal}
        });
    }

    spdlog::info("[get_characters] 共 {} 个角色", chars.size());
    return {{"characters", std::move(chars)}};
}

// ===========================================================================
// CreateCharacterTool
// ===========================================================================

json CreateCharacterTool::parameters() const {
    return utils::schema::object({
        {"name", utils::schema::stringProp("角色姓名")},
        {"role", utils::schema::stringEnum("角色定位",
            {"protagonist", "antagonist", "supporting", "minor"})},
        {"personality", utils::schema::stringProp("性格特征描述（可选）")},
        {"background", utils::schema::stringProp("背景故事（可选）")},
        {"goal", utils::schema::stringProp("核心目标（可选）")},
        {"motivation", utils::schema::stringProp("行为动机（可选）")},
        {"appearance", utils::schema::stringProp("外貌描述（可选）")},
        {"age", utils::schema::stringProp("年龄段/年龄（可选）")},
        {"arc", utils::schema::stringProp("角色弧线概述（可选）")},
        {"speaking_style", utils::schema::stringProp("说话风格（可选）")},
        {"internal_conflict", utils::schema::stringProp("内在冲突（可选）")},
        {"external_conflict", utils::schema::stringProp("外部冲突（可选）")}
    }, {"name"});
}

json CreateCharacterTool::execute(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) {
        return {{"error", "角色姓名不能为空"}};
    }

    // 检查重名
    for (const auto& ch : project_->characters) {
        if (ch.name == name) {
            return {{"error", "角色 '" + name + "' 已存在（ID: " + ch.id + "）"}};
        }
    }

    // 生成 ID
    int max_num = 0;
    for (const auto& ch : project_->characters) {
        if (ch.id.size() >= 5 && ch.id.substr(0, 5) == "char-") {
            try { max_num = std::max(max_num, std::stoi(ch.id.substr(5))); }
            catch (...) {}
        }
    }

    Character new_ch;
    new_ch.id = "char-" + std::to_string(max_num + 1);
    // 补零
    if (max_num + 1 < 10)       new_ch.id = "char-00" + std::to_string(max_num + 1);
    else if (max_num + 1 < 100) new_ch.id = "char-0" + std::to_string(max_num + 1);
    new_ch.name = name;
    new_ch.role = args.value("role", "supporting");

    // ── 可选叙事字段 ──
    new_ch.personality       = args.value("personality", "");
    new_ch.background        = args.value("background", "");
    new_ch.goal              = args.value("goal", "");
    new_ch.motivation        = args.value("motivation", "");
    new_ch.appearance        = args.value("appearance", "");
    new_ch.age               = args.value("age", "");
    new_ch.arc               = args.value("arc", "");
    new_ch.speaking_style    = args.value("speaking_style", "");
    new_ch.internal_conflict = args.value("internal_conflict", "");
    new_ch.external_conflict = args.value("external_conflict", "");

    project_->characters.push_back(new_ch);
    ProjectIO::save(*project_);

    spdlog::info("[create_character] {} '{}' (role={})", new_ch.id, name, new_ch.role);

    return {
        {"success", true},
        {"character", {
            {"id", new_ch.id},
            {"name", new_ch.name},
            {"role", new_ch.role}
        }}
    };
}

// ===========================================================================
// UpdateCharacterTool
// ===========================================================================

json UpdateCharacterTool::parameters() const {
    return utils::schema::object({
        {"character_id", utils::schema::stringProp("角色 ID")},
        {"fields", utils::schema::object({}, {})} // 任意字段
    }, {"character_id", "fields"});
}

/// 支持的 Character 字符串字段名列表（用于安全校验和赋值）
static const std::set<std::string> kUpdatableStringFields = {
    "name", "role", "age", "appearance", "personality", "background",
    "goal", "motivation", "internal_conflict", "external_conflict",
    "secret", "fear", "misbelief", "speaking_style", "arc", "notes"
};

/// 支持的字符串数组字段
static const std::set<std::string> kUpdatableArrayFields = {
    "traits", "core_values", "taboos", "chapter_appearances"
};

json UpdateCharacterTool::execute(const json& args) {
    std::string id = args.value("character_id", "");
    auto* ch = findCharacter(project_->characters, id);
    if (!ch) {
        return {{"error", "角色 '" + id + "' 不存在"}};
    }

    const json& fields = args["fields"];
    if (!fields.is_object() || fields.empty()) {
        return {{"error", "fields 必须是非空的对象"}};
    }

    // 逐字段更新 — 用指针到成员的 map 避免冗长的 if-else 链
    using StrField = std::string Character::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"name", &Character::name},
        {"role", &Character::role},
        {"age", &Character::age},
        {"appearance", &Character::appearance},
        {"personality", &Character::personality},
        {"background", &Character::background},
        {"goal", &Character::goal},
        {"motivation", &Character::motivation},
        {"internal_conflict", &Character::internal_conflict},
        {"external_conflict", &Character::external_conflict},
        {"secret", &Character::secret},
        {"fear", &Character::fear},
        {"misbelief", &Character::misbelief},
        {"speaking_style", &Character::speaking_style},
        {"arc", &Character::arc},
        {"notes", &Character::notes},
    };

    using ArrField = std::vector<std::string> Character::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"traits", &Character::traits},
        {"core_values", &Character::core_values},
        {"taboos", &Character::taboos},
        {"chapter_appearances", &Character::chapter_appearances},
    };

    std::vector<std::string> updated;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const std::string& key = it.key();
        const json& value = it.value();

        if (auto si = kStringMap.find(key); si != kStringMap.end() && value.is_string()) {
            ch->*si->second = value.get<std::string>();
            updated.push_back(key);
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && value.is_array()) {
            auto& arr = ch->*ai->second;
            arr.clear();
            for (const auto& v : value) arr.push_back(v.get<std::string>());
            updated.push_back(key);
        }
        // 不在白名单中的字段 → 静默忽略
    }

    if (updated.empty()) {
        return {{"error", "没有可以更新的字段"}};
    }

    ProjectIO::save(*project_);
    // 拼接更新字段名用于日志
    std::string fields_str;
    for (size_t i = 0; i < updated.size(); ++i) {
        if (i > 0) fields_str += ", ";
        fields_str += updated[i];
    }
    spdlog::info("[update_character] {} 更新 {} 个字段: {}", id, updated.size(), fields_str);

    return {
        {"success", true},
        {"character", {
            {"id", ch->id},
            {"name", ch->name},
            {"updated_fields", updated}
        }}
    };
}

// ===========================================================================
// DeleteCharacterTool
// ===========================================================================

json DeleteCharacterTool::parameters() const {
    return utils::schema::object({
        {"character_id", utils::schema::stringProp("要删除的角色 ID")}
    }, {"character_id"});
}

json DeleteCharacterTool::execute(const json& args) {
    const std::string cid = args.value("character_id", "");
    if (cid.empty()) return {{"error", "character_id 不能为空"}};

    auto* ch = findCharacter(project_->characters, cid);
    if (!ch) return {{"error", "角色不存在: " + cid}};

    project_->characters.erase(
        std::remove_if(project_->characters.begin(), project_->characters.end(),
            [&](const Character& c) { return c.id == cid; }),
        project_->characters.end());

    // 级联清理
    int cascade_rel = 0, cascade_setting = 0, cascade_pt = 0, cascade_vol = 0;
    int cascade_ch = 0, cascade_scene = 0;

    // 其他角色的 Relationships
    for (auto& cr : project_->characters) {
        auto before = cr.relationships.size();
        cr.relationships.erase(
            std::remove_if(cr.relationships.begin(), cr.relationships.end(),
                [&](const Relationship& r) { return r.target_character_id == cid; }),
            cr.relationships.end());
        if (cr.relationships.size() < before) cascade_rel += static_cast<int>(before - cr.relationships.size());
    }
    // Setting.related_characters
    for (auto& s : project_->settings) {
        auto before = s.related_characters.size();
        s.related_characters.erase(std::remove(s.related_characters.begin(), s.related_characters.end(), cid), s.related_characters.end());
        if (s.related_characters.size() < before) cascade_setting += static_cast<int>(before - s.related_characters.size());
    }
    // PlotThread.related_characters
    for (auto& pt : project_->outline.plot_threads) {
        auto before = pt.related_characters.size();
        pt.related_characters.erase(std::remove(pt.related_characters.begin(), pt.related_characters.end(), cid), pt.related_characters.end());
        if (pt.related_characters.size() < before) cascade_pt += static_cast<int>(before - pt.related_characters.size());
    }
    // Volume.focus_characters
    for (auto& v : project_->outline.volumes) {
        auto before = v.focus_characters.size();
        v.focus_characters.erase(std::remove(v.focus_characters.begin(), v.focus_characters.end(), cid), v.focus_characters.end());
        if (v.focus_characters.size() < before) cascade_vol += static_cast<int>(before - v.focus_characters.size());
    }
    // Chapter: pov_characters / focus_characters（数组）+ Scene: pov_character_id（单值）/ participants（数组）
    for (auto& chapter : project_->outline.chapters) {
        auto& pv = chapter.pov_characters;
        auto b1 = pv.size();
        pv.erase(std::remove(pv.begin(), pv.end(), cid), pv.end());
        if (pv.size() < b1) cascade_ch += static_cast<int>(b1 - pv.size());
        auto& fc = chapter.focus_characters;
        auto b2 = fc.size();
        fc.erase(std::remove(fc.begin(), fc.end(), cid), fc.end());
        if (fc.size() < b2) cascade_ch += static_cast<int>(b2 - fc.size());
        for (auto& sc : chapter.scenes) {
            if (sc.pov_character_id == cid) { sc.pov_character_id.clear(); ++cascade_scene; }
            auto& sp = sc.participants;
            auto b3 = sp.size();
            sp.erase(std::remove(sp.begin(), sp.end(), cid), sp.end());
            if (sp.size() < b3) cascade_scene += static_cast<int>(b3 - sp.size());
        }
    }

    ProjectIO::save(*project_);
    spdlog::info("[delete_character] {} 已删除 (cascade: rel={} st={} pt={} vol={} ch={} sc={})",
                 cid, cascade_rel, cascade_setting, cascade_pt, cascade_vol, cascade_ch, cascade_scene);
    return {
        {"success", true},
        {"deleted_id", cid},
        {"cascade", {
            {"relationships_cleaned", cascade_rel},
            {"settings_cleaned", cascade_setting},
            {"plot_threads_cleaned", cascade_pt},
            {"volumes_cleaned", cascade_vol},
            {"chapters_cleaned", cascade_ch + cascade_scene}
        }}
    };
}

// ===========================================================================
// UpdateCharacterRelationshipsTool
// ===========================================================================

json UpdateCharacterRelationshipsTool::parameters() const {
    return utils::schema::object({
        {"character_id", utils::schema::stringProp("角色 ID")},
        {"relationships", utils::schema::stringArrayProp("完整关系列表（替换全部），每项为 object 含 target_character_id/type/description 等字段")}
    }, {"character_id", "relationships"});
}

json UpdateCharacterRelationshipsTool::execute(const json& args) {
    auto* ch = findCharacter(project_->characters, args.value("character_id", ""));
    if (!ch) return {{"error", "角色不存在"}};

    const auto& rels = args["relationships"];
    if (!rels.is_array()) return {{"error", "relationships 必须是数组"}};

    std::vector<Relationship> parsed;
    for (const auto& r : rels) {
        if (!r.is_object()) continue;
        parsed.push_back(Relationship{});
        auto& rel = parsed.back();
        rel.target_character_id = r.value("target_character_id", "");
        rel.type               = r.value("type", "");
        rel.description        = r.value("description", "");
        rel.public_status      = r.value("public_status", "");
        rel.private_feeling    = r.value("private_feeling", "");
        rel.status             = r.value("status", "active");
        rel.tension            = r.value("tension", 0);
    }

    ch->relationships = std::move(parsed);
    ProjectIO::save(*project_);
    spdlog::info("[update_character_relationships] {} 更新 {} 条关系", ch->id, ch->relationships.size());
    return {{"success", true}, {"character_id", ch->id}, {"relationship_count", ch->relationships.size()}};
}

} // namespace agent

REGISTER_TOOL(agent::GetCharacterTool, "get_character", get_character)
REGISTER_TOOL(agent::ListCharactersTool, "get_characters", get_characters)
REGISTER_TOOL(agent::CreateCharacterTool, "create_character", create_character)
REGISTER_TOOL(agent::UpdateCharacterTool, "update_character", update_character)
REGISTER_TOOL(agent::DeleteCharacterTool, "delete_character", delete_character)
REGISTER_TOOL(agent::UpdateCharacterRelationshipsTool, "update_character_relationships", update_character_relationships)
