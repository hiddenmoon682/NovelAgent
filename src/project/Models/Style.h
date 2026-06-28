#pragma once

/// Style — 写作风格配置。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Style {
    std::string tone = "neutral", pacing = "moderate", pov = "third_person_limited";
    std::string tense = "past", prose_style = "literary", dialogue_style = "naturalistic";
    std::string narrative_distance = "close", sentence_length = "varied", vocabulary = "rich";
    std::string voice_reference, show_vs_tell_bias = "balanced", dialogue_density = "moderate";
    std::string description_density = "moderate", introspection_density = "moderate";
    std::string humor_level = "low", sensory_focus, chapter_opening_style, chapter_ending_style;
    int chapter_length_target = 4000;
    std::vector<std::string> forbidden_phrases, forbidden_tropes;
    std::string notes;
    std::map<std::string, nlohmann::json> metadata;
};

inline void to_json(nlohmann::json& j, const Style& s) {
    j = nlohmann::json{
        {"tone", s.tone}, {"pacing", s.pacing}, {"pov", s.pov},
        {"tense", s.tense}, {"prose_style", s.prose_style},
        {"dialogue_style", s.dialogue_style}, {"narrative_distance", s.narrative_distance},
        {"chapter_length_target", s.chapter_length_target},
        {"sentence_length", s.sentence_length}, {"vocabulary", s.vocabulary},
        {"voice_reference", s.voice_reference}, {"show_vs_tell_bias", s.show_vs_tell_bias},
        {"dialogue_density", s.dialogue_density}, {"description_density", s.description_density},
        {"introspection_density", s.introspection_density}, {"humor_level", s.humor_level},
        {"sensory_focus", s.sensory_focus}, {"forbidden_phrases", s.forbidden_phrases},
        {"forbidden_tropes", s.forbidden_tropes},
        {"chapter_opening_style", s.chapter_opening_style},
        {"chapter_ending_style", s.chapter_ending_style},
        {"notes", s.notes}, {"metadata", s.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Style& s) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "tone", "pacing", "pov", "tense", "prose_style", "dialogue_style",
        "narrative_distance", "chapter_length_target", "sentence_length",
        "vocabulary", "voice_reference", "show_vs_tell_bias",
        "dialogue_density", "description_density", "introspection_density",
        "humor_level", "sensory_focus", "forbidden_phrases",
        "forbidden_tropes", "chapter_opening_style", "chapter_ending_style",
        "notes", "metadata"
    };
    s.tone = utils::json::getOrDefault(j, "tone", std::string{"neutral"});
    s.pacing = utils::json::getOrDefault(j, "pacing", std::string{"moderate"});
    s.pov = utils::json::getOrDefault(j, "pov", std::string{"third_person_limited"});
    s.tense = utils::json::getOrDefault(j, "tense", std::string{"past"});
    s.prose_style = utils::json::getOrDefault(j, "prose_style", std::string{"literary"});
    s.dialogue_style = utils::json::getOrDefault(j, "dialogue_style", std::string{"naturalistic"});
    s.narrative_distance = utils::json::getOrDefault(j, "narrative_distance", std::string{"close"});
    s.chapter_length_target = utils::json::getOrDefault(j, "chapter_length_target", 4000);
    s.sentence_length = utils::json::getOrDefault(j, "sentence_length", std::string{"varied"});
    s.vocabulary = utils::json::getOrDefault(j, "vocabulary", std::string{"rich"});
    s.voice_reference = utils::json::getOrDefault(j, "voice_reference", std::string{});
    s.show_vs_tell_bias = utils::json::getOrDefault(j, "show_vs_tell_bias", std::string{"balanced"});
    s.dialogue_density = utils::json::getOrDefault(j, "dialogue_density", std::string{"moderate"});
    s.description_density = utils::json::getOrDefault(j, "description_density", std::string{"moderate"});
    s.introspection_density = utils::json::getOrDefault(j, "introspection_density", std::string{"moderate"});
    s.humor_level = utils::json::getOrDefault(j, "humor_level", std::string{"low"});
    s.sensory_focus = utils::json::getOrDefault(j, "sensory_focus", std::string{});
    s.forbidden_phrases = utils::json::getOrDefault(j, "forbidden_phrases", std::vector<std::string>{});
    s.forbidden_tropes = utils::json::getOrDefault(j, "forbidden_tropes", std::vector<std::string>{});
    s.chapter_opening_style = utils::json::getOrDefault(j, "chapter_opening_style", std::string{});
    s.chapter_ending_style = utils::json::getOrDefault(j, "chapter_ending_style", std::string{});
    s.notes = utils::json::getOrDefault(j, "notes", std::string{});
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
