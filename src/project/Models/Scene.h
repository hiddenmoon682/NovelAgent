#pragma once

/// Scene — 章节内部的最小戏剧单元。

#include "project/Models/GenerationControl.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Scene {
    std::string id, title, summary, goal, conflict, outcome;
    std::string turning_point, emotional_beat, reveal, foreshadowing, payoff;
    std::string pov_character_id, location_id, time_marker;
    std::vector<std::string> participants, plot_thread_ids, tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Scene& s) {
    j = nlohmann::json{
        {"id", s.id}, {"title", s.title}, {"summary", s.summary},
        {"goal", s.goal}, {"conflict", s.conflict}, {"outcome", s.outcome},
        {"turning_point", s.turning_point}, {"emotional_beat", s.emotional_beat},
        {"reveal", s.reveal}, {"foreshadowing", s.foreshadowing}, {"payoff", s.payoff},
        {"pov_character_id", s.pov_character_id}, {"location_id", s.location_id},
        {"time_marker", s.time_marker}, {"participants", s.participants},
        {"plot_thread_ids", s.plot_thread_ids}, {"tags", s.tags},
        {"generation", s.generation}, {"metadata", s.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Scene& s) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "title", "summary", "goal", "conflict", "outcome",
        "turning_point", "emotional_beat", "reveal", "foreshadowing",
        "payoff", "pov_character_id", "location_id", "time_marker",
        "participants", "plot_thread_ids", "tags", "generation", "metadata"
    };
    s.id = utils::json::getOrDefault(j, "id", std::string{});
    s.title = utils::json::getOrDefault(j, "title", std::string{});
    s.summary = utils::json::getOrDefault(j, "summary", std::string{});
    s.goal = utils::json::getOrDefault(j, "goal", std::string{});
    s.conflict = utils::json::getOrDefault(j, "conflict", std::string{});
    s.outcome = utils::json::getOrDefault(j, "outcome", std::string{});
    s.turning_point = utils::json::getOrDefault(j, "turning_point", std::string{});
    s.emotional_beat = utils::json::getOrDefault(j, "emotional_beat", std::string{});
    s.reveal = utils::json::getOrDefault(j, "reveal", std::string{});
    s.foreshadowing = utils::json::getOrDefault(j, "foreshadowing", std::string{});
    s.payoff = utils::json::getOrDefault(j, "payoff", std::string{});
    s.pov_character_id = utils::json::getOrDefault(j, "pov_character_id", std::string{});
    s.location_id = utils::json::getOrDefault(j, "location_id", std::string{});
    s.time_marker = utils::json::getOrDefault(j, "time_marker", std::string{});
    s.participants = utils::json::getOrDefault(j, "participants", std::vector<std::string>{});
    s.plot_thread_ids = utils::json::getOrDefault(j, "plot_thread_ids", std::vector<std::string>{});
    s.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    s.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
