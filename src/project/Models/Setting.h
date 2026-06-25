#pragma once

/// Setting — 世界观中的地点、组织或物品。

#include "project/Models/GenerationControl.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Setting {
    std::string id, name, category = "location", description, story_function, sensory_profile, notes;
    std::vector<std::string> related_characters, related_plot_threads, related_rule_ids, tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Setting& s) {
    j = nlohmann::json{
        {"id", s.id}, {"name", s.name}, {"category", s.category},
        {"description", s.description}, {"story_function", s.story_function},
        {"sensory_profile", s.sensory_profile},
        {"related_characters", s.related_characters},
        {"related_plot_threads", s.related_plot_threads},
        {"related_rule_ids", s.related_rule_ids}, {"notes", s.notes},
        {"tags", s.tags}, {"generation", s.generation}, {"metadata", s.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Setting& s) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "category", "description", "story_function",
        "sensory_profile", "related_characters", "related_plot_threads",
        "related_rule_ids", "notes", "tags", "generation", "metadata"
    };
    s.id = utils::json::getOrDefault(j, "id", std::string{});
    s.name = utils::json::getOrDefault(j, "name", std::string{});
    s.category = utils::json::getOrDefault(j, "category", std::string{"location"});
    s.description = utils::json::getOrDefault(j, "description", std::string{});
    s.story_function = utils::json::getOrDefault(j, "story_function", std::string{});
    s.sensory_profile = utils::json::getOrDefault(j, "sensory_profile", std::string{});
    s.related_characters = utils::json::getOrDefault(j, "related_characters", std::vector<std::string>{});
    s.related_plot_threads = utils::json::getOrDefault(j, "related_plot_threads", std::vector<std::string>{});
    s.related_rule_ids = utils::json::getOrDefault(j, "related_rule_ids", std::vector<std::string>{});
    s.notes = utils::json::getOrDefault(j, "notes", std::string{});
    s.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    s.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
