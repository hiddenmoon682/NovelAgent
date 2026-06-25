#pragma once

/// Chapter — 章节（写给 AI 的"章级创作简报"）。

#include "project/Models/GenerationControl.h"
#include "project/Models/Scene.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Chapter {
    std::string id, title, synopsis, goal, conflict, outcome;
    int order = 0;
    std::string turning_point, hook, reveal, foreshadowing, payoff, emotional_beat;
    std::string location_id, time_marker, volume_id;
    std::vector<Scene> scenes;
    std::vector<std::string> pov_characters, key_events, themes, active_plot_threads;
    std::vector<std::string> focus_characters, focus_settings;
    std::string status = "outlined", file_path;
    int word_count = 0;
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Chapter& c) {
    j = nlohmann::json{
        {"id", c.id}, {"title", c.title}, {"order", c.order},
        {"synopsis", c.synopsis}, {"goal", c.goal}, {"conflict", c.conflict},
        {"outcome", c.outcome}, {"turning_point", c.turning_point},
        {"hook", c.hook}, {"reveal", c.reveal}, {"foreshadowing", c.foreshadowing},
        {"payoff", c.payoff}, {"emotional_beat", c.emotional_beat},
        {"location_id", c.location_id}, {"time_marker", c.time_marker},
        {"scenes", c.scenes}, {"pov_characters", c.pov_characters},
        {"key_events", c.key_events}, {"themes", c.themes},
        {"active_plot_threads", c.active_plot_threads},
        {"focus_characters", c.focus_characters}, {"focus_settings", c.focus_settings},
        {"volume_id", c.volume_id}, {"status", c.status},
        {"word_count", c.word_count}, {"file_path", c.file_path},
        {"tags", c.tags}, {"generation", c.generation}, {"metadata", c.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Chapter& c) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "title", "order", "synopsis", "goal", "conflict", "outcome",
        "turning_point", "hook", "reveal", "foreshadowing", "payoff",
        "emotional_beat", "location_id", "time_marker", "scenes",
        "pov_characters", "key_events", "themes", "active_plot_threads",
        "focus_characters", "focus_settings", "volume_id", "status", "word_count",
        "file_path", "tags", "generation", "metadata"
    };
    c.id = utils::json::getOrDefault(j, "id", std::string{});
    c.title = utils::json::getOrDefault(j, "title", std::string{});
    c.order = utils::json::getOrDefault(j, "order", 0);
    c.synopsis = utils::json::getOrDefault(j, "synopsis", std::string{});
    c.goal = utils::json::getOrDefault(j, "goal", std::string{});
    c.conflict = utils::json::getOrDefault(j, "conflict", std::string{});
    c.outcome = utils::json::getOrDefault(j, "outcome", std::string{});
    c.turning_point = utils::json::getOrDefault(j, "turning_point", std::string{});
    c.hook = utils::json::getOrDefault(j, "hook", std::string{});
    c.reveal = utils::json::getOrDefault(j, "reveal", std::string{});
    c.foreshadowing = utils::json::getOrDefault(j, "foreshadowing", std::string{});
    c.payoff = utils::json::getOrDefault(j, "payoff", std::string{});
    c.emotional_beat = utils::json::getOrDefault(j, "emotional_beat", std::string{});
    c.location_id = utils::json::getOrDefault(j, "location_id", std::string{});
    c.time_marker = utils::json::getOrDefault(j, "time_marker", std::string{});
    c.scenes = utils::json::getOrDefault(j, "scenes", std::vector<Scene>{});
    c.pov_characters = utils::json::getOrDefault(j, "pov_characters", std::vector<std::string>{});
    c.key_events = utils::json::getOrDefault(j, "key_events", std::vector<std::string>{});
    c.themes = utils::json::getOrDefault(j, "themes", std::vector<std::string>{});
    c.active_plot_threads = utils::json::getOrDefault(j, "active_plot_threads", std::vector<std::string>{});
    c.focus_characters = utils::json::getOrDefault(j, "focus_characters", std::vector<std::string>{});
    c.focus_settings = utils::json::getOrDefault(j, "focus_settings", std::vector<std::string>{});
    c.volume_id = utils::json::getOrDefault(j, "volume_id", std::string{});
    c.status = utils::json::getOrDefault(j, "status", std::string{"outlined"});
    c.word_count = utils::json::getOrDefault(j, "word_count", 0);
    c.file_path = utils::json::getOrDefault(j, "file_path", std::string{});
    c.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    c.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
