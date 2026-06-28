#pragma once

/// WorldRule — 世界规则（适合奇幻、科幻、悬疑等需要"规则一致性"的小说）。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct WorldRule {
    std::string id, name, summary, limitations, costs, exceptions, known_by;
    std::vector<std::string> related_settings;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const WorldRule& r) {
    j = nlohmann::json{
        {"id", r.id}, {"name", r.name}, {"summary", r.summary},
        {"limitations", r.limitations}, {"costs", r.costs},
        {"exceptions", r.exceptions}, {"known_by", r.known_by},
        {"related_settings", r.related_settings},
        {"metadata", r.metadata}
    };
}

inline void from_json(const nlohmann::json& j, WorldRule& r) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "summary", "limitations", "costs", "exceptions",
        "known_by", "related_settings", "metadata"
    };
    r.id = utils::json::getOrDefault(j, "id", std::string{});
    r.name = utils::json::getOrDefault(j, "name", std::string{});
    r.summary = utils::json::getOrDefault(j, "summary", std::string{});
    r.limitations = utils::json::getOrDefault(j, "limitations", std::string{});
    r.costs = utils::json::getOrDefault(j, "costs", std::string{});
    r.exceptions = utils::json::getOrDefault(j, "exceptions", std::string{});
    r.known_by = utils::json::getOrDefault(j, "known_by", std::string{});
    r.related_settings = utils::json::getOrDefault(j, "related_settings", std::vector<std::string>{});
    r.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
