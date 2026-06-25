#pragma once

/// Relationship — 角色之间的关系。

#include "project/Models/GenerationControl.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Relationship {
    std::string target_character_id, type, description, public_status, private_feeling;
    std::string status = "active";
    int tension = 0;
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Relationship& r) {
    j = nlohmann::json{
        {"target_character_id", r.target_character_id}, {"type", r.type},
        {"description", r.description}, {"public_status", r.public_status},
        {"private_feeling", r.private_feeling}, {"status", r.status},
        {"tension", r.tension}, {"tags", r.tags},
        {"generation", r.generation}, {"metadata", r.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Relationship& r) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "target_character_id", "type", "description", "public_status",
        "private_feeling", "status", "tension", "tags", "generation", "metadata"
    };
    r.target_character_id = utils::json::getOrDefault(j, "target_character_id", std::string{});
    r.type = utils::json::getOrDefault(j, "type", std::string{});
    r.description = utils::json::getOrDefault(j, "description", std::string{});
    r.public_status = utils::json::getOrDefault(j, "public_status", std::string{});
    r.private_feeling = utils::json::getOrDefault(j, "private_feeling", std::string{});
    r.status = utils::json::getOrDefault(j, "status", std::string{"active"});
    r.tension = utils::json::getOrDefault(j, "tension", 0);
    r.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    r.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    r.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
