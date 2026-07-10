#pragma once

// Outline — 大纲。

#include "project/Models/ModelDetail.h"
#include "project/Models/Volume.h"
#include "project/Models/PlotThread.h"
#include "project/Models/Chapter.h"

#include <map>
#include <string>
#include <vector>

struct Outline {
    std::string premise, story_structure;
    std::vector<std::string> act_summaries;
    std::vector<Volume> volumes;
    std::vector<PlotThread> plot_threads;
    std::vector<Chapter> chapters;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Outline& o) {
    j = nlohmann::json{
        {"premise", o.premise}, {"story_structure", o.story_structure},
        {"act_summaries", o.act_summaries}, {"volumes", o.volumes},
        {"plot_threads", o.plot_threads}, {"chapters", o.chapters},
        {"metadata", o.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Outline& o) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "premise", "story_structure", "act_summaries", "volumes", "plot_threads",
        "chapters", "metadata"
    };
    o.premise = utils::json::getOrDefault(j, "premise", std::string{});
    o.story_structure = utils::json::getOrDefault(j, "story_structure", std::string{});
    o.act_summaries = utils::json::getOrDefault(j, "act_summaries", std::vector<std::string>{});
    o.volumes = utils::json::getOrDefault(j, "volumes", std::vector<Volume>{});
    o.plot_threads = utils::json::getOrDefault(j, "plot_threads", std::vector<PlotThread>{});
    o.chapters = utils::json::getOrDefault(j, "chapters", std::vector<Chapter>{});
    o.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
