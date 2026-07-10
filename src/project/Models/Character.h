#pragma once

// Character — 角色完整档案。

#include "project/Models/Relationship.h"
#include "project/Models/CharacterDevelopment.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Character {
    std::string id, name, role = "supporting", age, appearance, personality;
    std::string background, goal, motivation, internal_conflict, external_conflict;
    std::string secret, fear, misbelief, speaking_style;
    std::vector<std::string> traits, core_values, taboos;
    std::vector<Relationship> relationships;
    std::vector<std::string> chapter_appearances;
    std::string arc, notes;
    std::vector<CharacterDevelopment> development;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Character& c) {
    j = nlohmann::json{
        {"id", c.id}, {"name", c.name}, {"role", c.role}, {"age", c.age},
        {"appearance", c.appearance}, {"personality", c.personality},
        {"background", c.background}, {"goal", c.goal}, {"motivation", c.motivation},
        {"internal_conflict", c.internal_conflict}, {"external_conflict", c.external_conflict},
        {"secret", c.secret}, {"fear", c.fear}, {"misbelief", c.misbelief},
        {"speaking_style", c.speaking_style}, {"traits", c.traits},
        {"core_values", c.core_values}, {"taboos", c.taboos},
        {"relationships", c.relationships}, {"chapter_appearances", c.chapter_appearances},
        {"arc", c.arc}, {"notes", c.notes}, {"development", c.development},
        {"metadata", c.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Character& c) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "role", "age", "appearance", "personality",
        "background", "goal", "motivation", "internal_conflict",
        "external_conflict", "secret", "fear", "misbelief",
        "speaking_style", "traits", "core_values", "taboos",
        "relationships", "chapter_appearances", "arc", "notes",
        "development", "metadata"
    };
    c.id = utils::json::getOrDefault(j, "id", std::string{});
    c.name = utils::json::getOrDefault(j, "name", std::string{});
    c.role = utils::json::getOrDefault(j, "role", std::string{"supporting"});
    c.age = utils::json::getOrDefault(j, "age", std::string{});
    c.appearance = utils::json::getOrDefault(j, "appearance", std::string{});
    c.personality = utils::json::getOrDefault(j, "personality", std::string{});
    c.background = utils::json::getOrDefault(j, "background", std::string{});
    c.goal = utils::json::getOrDefault(j, "goal", std::string{});
    c.motivation = utils::json::getOrDefault(j, "motivation", std::string{});
    c.internal_conflict = utils::json::getOrDefault(j, "internal_conflict", std::string{});
    c.external_conflict = utils::json::getOrDefault(j, "external_conflict", std::string{});
    c.secret = utils::json::getOrDefault(j, "secret", std::string{});
    c.fear = utils::json::getOrDefault(j, "fear", std::string{});
    c.misbelief = utils::json::getOrDefault(j, "misbelief", std::string{});
    c.speaking_style = utils::json::getOrDefault(j, "speaking_style", std::string{});
    c.traits = utils::json::getOrDefault(j, "traits", std::vector<std::string>{});
    c.core_values = utils::json::getOrDefault(j, "core_values", std::vector<std::string>{});
    c.taboos = utils::json::getOrDefault(j, "taboos", std::vector<std::string>{});
    c.relationships = utils::json::getOrDefault(j, "relationships", std::vector<Relationship>{});
    c.chapter_appearances = utils::json::getOrDefault(j, "chapter_appearances", std::vector<std::string>{});
    c.arc = utils::json::getOrDefault(j, "arc", std::string{});
    c.notes = utils::json::getOrDefault(j, "notes", std::string{});
    c.development = utils::json::getOrDefault(j, "development", std::vector<CharacterDevelopment>{});
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
