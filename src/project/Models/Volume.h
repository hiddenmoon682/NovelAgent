#pragma once

/// Volume — 卷纲，写给 AI 的"卷级创作简报"。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Volume {
    std::string id, title, summary, theme, goal, start_chapter_id, end_chapter_id;
    int order = 0;
    std::vector<std::string> key_events, focus_characters, active_plot_threads;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Volume& v) {
    j = nlohmann::json{
        {"id", v.id}, {"title", v.title}, {"order", v.order},
        {"summary", v.summary}, {"theme", v.theme}, {"goal", v.goal},
        {"start_chapter_id", v.start_chapter_id}, {"end_chapter_id", v.end_chapter_id},
        {"key_events", v.key_events}, {"focus_characters", v.focus_characters},
        {"active_plot_threads", v.active_plot_threads},
        {"metadata", v.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Volume& v) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "title", "order", "summary", "theme", "goal",
        "start_chapter_id", "end_chapter_id", "key_events",
        "focus_characters", "active_plot_threads", "metadata"
    };
    v.id = utils::json::getOrDefault(j, "id", std::string{});
    v.title = utils::json::getOrDefault(j, "title", std::string{});
    v.order = utils::json::getOrDefault(j, "order", 0);
    v.summary = utils::json::getOrDefault(j, "summary", std::string{});
    v.theme = utils::json::getOrDefault(j, "theme", std::string{});
    v.goal = utils::json::getOrDefault(j, "goal", std::string{});
    v.start_chapter_id = utils::json::getOrDefault(j, "start_chapter_id", std::string{});
    v.end_chapter_id = utils::json::getOrDefault(j, "end_chapter_id", std::string{});
    v.key_events = utils::json::getOrDefault(j, "key_events", std::vector<std::string>{});
    v.focus_characters = utils::json::getOrDefault(j, "focus_characters", std::vector<std::string>{});
    v.active_plot_threads = utils::json::getOrDefault(j, "active_plot_threads", std::vector<std::string>{});
    v.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
