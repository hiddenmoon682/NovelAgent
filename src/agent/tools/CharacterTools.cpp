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
    // role 使用枚举限制可选值
    return utils::schema::object({
        {"name", utils::schema::stringProp("角色姓名")},
        {"role", utils::schema::stringEnum("角色定位",
            {"protagonist", "antagonist", "supporting", "minor"})}
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

} // namespace agent

REGISTER_TOOL(agent::GetCharacterTool, "get_character", get_character)
REGISTER_TOOL(agent::ListCharactersTool, "get_characters", get_characters)
REGISTER_TOOL(agent::CreateCharacterTool, "create_character", create_character)
REGISTER_TOOL(agent::UpdateCharacterTool, "update_character", update_character)
