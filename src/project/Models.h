#pragma once

// All data models for NovelAgent.
// Each struct uses NLOHMANN_DEFINE_TYPE_INTRUSIVE for automatic JSON
// serialization — field order must match the JSON schema.
//
// Struct hierarchy:
//   Project ── Outline ── PlotThread
//           │             ├── Chapter
//           ├── Character[]
//           ├── Setting[]
//           └── Style

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

// ──────────────────────────────────────────────
//  Chapter — a single entry in the outline
// ──────────────────────────────────────────────

struct Chapter {
    std::string id;                       // e.g. "ch-001"
    std::string title;                    // e.g. "The Discovery"
    int order = 0;                        // position in the novel
    std::string synopsis;                 // 1-2 sentence summary
    std::vector<std::string> scenes;      // ordered scene descriptions
    std::vector<std::string> pov_characters; // character IDs with POV
    std::vector<std::string> key_events;  // plot-significant beats
    std::vector<std::string> themes;      // e.g. "redemption", "betrayal"
    std::string status = "outlined";      // outlined|drafting|drafted|revised|final
    int word_count = 0;
    std::string file_path;                // chapters/001-title.md

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Chapter,
        id, title, order, synopsis, scenes, pov_characters,
        key_events, themes, status, word_count, file_path)
};

// ──────────────────────────────────────────────
//  Character — a person in the story
// ──────────────────────────────────────────────

struct Character {
    std::string id;
    std::string name;
    std::string role = "supporting";       // protagonist|antagonist|supporting|minor
    std::string age;                        // "28" or "late 20s" — free-form
    std::string appearance;                 // physical description
    std::string personality;                // traits and quirks
    std::string background;                 // backstory
    std::vector<std::string> traits;        // e.g. ["brave", "impulsive"]
    std::map<std::string, std::string> relationships; // char_id → description
    std::vector<std::string> chapter_appearances;     // chapter IDs
    std::string arc;                        // character arc summary
    std::string notes;                      // free-form notes

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Character,
        id, name, role, age, appearance, personality, background,
        traits, relationships, chapter_appearances, arc, notes)
};

// ──────────────────────────────────────────────
//  Setting — a world-building element
// ──────────────────────────────────────────────

struct Setting {
    std::string id;
    std::string name;                       // e.g. "Thorne University"
    std::string category = "location";      // location|organization|item|rule|other
    std::string description;                // free-form description
    std::map<std::string, std::string> attributes; // structured key-value pairs
    std::string notes;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Setting,
        id, name, category, description, attributes, notes)
};

// ──────────────────────────────────────────────
//  PlotThread — a subplot or narrative thread
// ──────────────────────────────────────────────

struct PlotThread {
    std::string id;
    std::string name;                       // e.g. "Main Quest", "Romance"
    std::string description;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlotThread, id, name, description)
};

// ──────────────────────────────────────────────
//  Outline — the full hierarchical outline
// ──────────────────────────────────────────────

struct Outline {
    std::string premise;                    // one-paragraph story premise
    std::vector<PlotThread> plot_threads;
    std::vector<Chapter> chapters;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Outline, premise, plot_threads, chapters)
};

// ──────────────────────────────────────────────
//  Style — writing style configuration
// ──────────────────────────────────────────────

struct Style {
    std::string tone = "neutral";           // atmospheric|dark|light|neutral|...
    std::string pacing = "moderate";        // slow|moderate|fast
    std::string pov = "third_person_limited"; // first_person|third_person_limited|third_person_omniscient
    std::string tense = "past";             // past|present
    std::string prose_style = "literary";   // literary|commercial|minimalist|descriptive
    std::string dialogue_style = "naturalistic"; // naturalistic|stylized|minimal
    std::string narrative_distance = "close";    // close|medium|distant
    int chapter_length_target = 4000;       // target words per chapter
    std::string sentence_length = "varied"; // short|medium|long|varied
    std::string vocabulary = "rich";        // simple|moderate|rich
    std::string notes;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Style,
        tone, pacing, pov, tense, prose_style, dialogue_style,
        narrative_distance, chapter_length_target, sentence_length,
        vocabulary, notes)
};

// ──────────────────────────────────────────────
//  Project — top-level novel metadata
// ──────────────────────────────────────────────

struct Project {
    int format_version = 1;                 // schema version for forward compat
    std::string title;
    std::string author;
    std::string description;
    std::vector<std::string> genre;
    int target_word_count = 0;
    int current_word_count = 0;
    std::string status = "planning";        // planning|in_progress|completed|on_hold
    std::string pov = "third_person_limited";
    std::string tense = "past";
    std::string created;                    // ISO 8601 timestamp
    std::string modified;                   // ISO 8601 timestamp

    // Runtime fields (not serialized — derived from filesystem)
    std::string path;

    // Sub-objects (loaded from separate JSON files, not embedded)
    Outline outline;
    std::vector<Character> characters;
    std::vector<Setting> settings;
    Style style;
};

// nlohmann serialization for Project — path is excluded (runtime only)
inline void to_json(nlohmann::json& j, const Project& p) {
    j = nlohmann::json{
        {"format_version", p.format_version},
        {"title", p.title},
        {"author", p.author},
        {"description", p.description},
        {"genre", p.genre},
        {"target_word_count", p.target_word_count},
        {"current_word_count", p.current_word_count},
        {"status", p.status},
        {"pov", p.pov},
        {"tense", p.tense},
        {"created", p.created},
        {"modified", p.modified}
    };
}

inline void from_json(const nlohmann::json& j, Project& p) {
    j.at("format_version").get_to(p.format_version);
    j.at("title").get_to(p.title);
    j.at("author").get_to(p.author);
    j.at("description").get_to(p.description);
    j.at("genre").get_to(p.genre);
    j.at("target_word_count").get_to(p.target_word_count);
    j.at("current_word_count").get_to(p.current_word_count);
    j.at("status").get_to(p.status);
    j.at("pov").get_to(p.pov);
    j.at("tense").get_to(p.tense);
    j.at("created").get_to(p.created);
    j.at("modified").get_to(p.modified);
}
