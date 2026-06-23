#pragma once

/// PlotThread — 剧情线。

#include "project/Models/GenerationControl.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct PlotThread {
    std::string id, name, description, type = "main", status = "planned";
    int priority = 0;
    std::string stakes, central_question, resolution, start_chapter_id, end_chapter_id;
    std::vector<std::string> related_characters, related_settings, tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const PlotThread& p) {
    j = nlohmann::json{
        {"id", p.id}, {"name", p.name}, {"description", p.description},
        {"type", p.type}, {"status", p.status}, {"priority", p.priority},
        {"stakes", p.stakes}, {"central_question", p.central_question},
        {"resolution", p.resolution}, {"start_chapter_id", p.start_chapter_id},
        {"end_chapter_id", p.end_chapter_id},
        {"related_characters", p.related_characters},
        {"related_settings", p.related_settings}, {"tags", p.tags},
        {"generation", p.generation}, {"metadata", p.metadata}
    };
}

inline void from_json(const nlohmann::json& j, PlotThread& p) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "description", "type", "status", "priority",
        "stakes", "central_question", "resolution", "start_chapter_id",
        "end_chapter_id", "related_characters", "related_settings",
        "tags", "generation", "metadata"
    };
    p.id = utils::json::getOrDefault(j, "id", std::string{});
    p.name = utils::json::getOrDefault(j, "name", std::string{});
    p.description = utils::json::getOrDefault(j, "description", std::string{});
    p.type = utils::json::getOrDefault(j, "type", std::string{"main"});
    p.status = utils::json::getOrDefault(j, "status", std::string{"planned"});
    p.priority = utils::json::getOrDefault(j, "priority", 0);
    p.stakes = utils::json::getOrDefault(j, "stakes", std::string{});
    p.central_question = utils::json::getOrDefault(j, "central_question", std::string{});
    p.resolution = utils::json::getOrDefault(j, "resolution", std::string{});
    p.start_chapter_id = utils::json::getOrDefault(j, "start_chapter_id", std::string{});
    p.end_chapter_id = utils::json::getOrDefault(j, "end_chapter_id", std::string{});
    p.related_characters = utils::json::getOrDefault(j, "related_characters", std::vector<std::string>{});
    p.related_settings = utils::json::getOrDefault(j, "related_settings", std::vector<std::string>{});
    p.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    p.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    p.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
