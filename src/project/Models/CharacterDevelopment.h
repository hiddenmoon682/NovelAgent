#pragma once

// CharacterDevelopment — 角色发展记录（轻量）。

#include "project/Models/ModelDetail.h"

#include <map>
#include <string>
#include <vector>

struct CharacterDevelopment {
    std::string id;
    std::string chapter_id;
    std::string summary;
    std::string category = "other";
    std::vector<std::string> affected_fields;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const CharacterDevelopment& d) {
    j = nlohmann::json{
        {"id", d.id}, {"chapter_id", d.chapter_id}, {"summary", d.summary},
        {"category", d.category}, {"affected_fields", d.affected_fields},
        {"metadata", d.metadata}
    };
}

inline void from_json(const nlohmann::json& j, CharacterDevelopment& d) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "chapter_id", "summary", "category", "affected_fields", "metadata"
    };
    d.id = utils::json::getOrDefault(j, "id", std::string{});
    d.chapter_id = utils::json::getOrDefault(j, "chapter_id", std::string{});
    d.summary = utils::json::getOrDefault(j, "summary", std::string{});
    d.category = utils::json::getOrDefault(j, "category", std::string{"other"});
    d.affected_fields = utils::json::getOrDefault(j, "affected_fields", std::vector<std::string>{});
    d.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
