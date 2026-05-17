#pragma once

// NovelAgent 的全部核心数据模型。
// 大多数结构体都使用 NLOHMANN_DEFINE_TYPE_INTRUSIVE 自动生成 JSON 序列化逻辑，
// 字段顺序需要与约定的 JSON 结构保持一致。
//
// 结构关系大致如下：
//   Project
//   |- Outline
//   |  |- PlotThread[]
//   |  `- Chapter[]
//   |- Character[]
//   |- Setting[]
//   `- Style

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

// 章节：大纲中的单个条目。
struct Chapter {
    std::string id;                          // 例如 "ch-001"
    std::string title;                       // 例如“发现”
    int order = 0;                           // 在整部小说中的顺序
    std::string synopsis;                    // 1 到 2 句摘要
    std::vector<std::string> scenes;         // 按顺序排列的场景描述
    std::vector<std::string> pov_characters; // 作为 POV 的角色 ID
    std::vector<std::string> key_events;     // 对剧情推进关键的事件
    std::vector<std::string> themes;         // 例如“救赎”“背叛”
    std::string status = "outlined";         // outlined|drafting|drafted|revised|final
    int word_count = 0;
    std::string file_path;                   // 例如 chapters/001-title.md

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Chapter,
        id, title, order, synopsis, scenes, pov_characters,
        key_events, themes, status, word_count, file_path)
};

// 角色：故事中的人物。
struct Character {
    std::string id;
    std::string name;
    std::string role = "supporting";           // protagonist|antagonist|supporting|minor
    std::string age;                           // 例如 "28" 或 "二十多岁末"
    std::string appearance;                    // 外貌描述
    std::string personality;                   // 性格与习惯
    std::string background;                    // 背景经历
    std::vector<std::string> traits;           // 例如 ["brave", "impulsive"]
    std::map<std::string, std::string> relationships; // 角色 ID -> 关系描述
    std::vector<std::string> chapter_appearances;     // 出现过的章节 ID
    std::string arc;                           // 角色弧光摘要
    std::string notes;                         // 自由补充备注

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Character,
        id, name, role, age, appearance, personality, background,
        traits, relationships, chapter_appearances, arc, notes)
};

// 设定：世界观中的地点、组织、物品或规则。
struct Setting {
    std::string id;
    std::string name;                          // 例如 "Thorne University"
    std::string category = "location";        // location|organization|item|rule|other
    std::string description;                  // 自由描述
    std::map<std::string, std::string> attributes; // 结构化键值对
    std::string notes;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Setting,
        id, name, category, description, attributes, notes)
};

// 剧情线：主线或支线叙事线程。
struct PlotThread {
    std::string id;
    std::string name;                          // 例如“主线任务”“感情线”
    std::string description;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlotThread, id, name, description)
};

// 大纲：完整的分层剧情结构。
struct Outline {
    std::string premise;                       // 一段式故事前提
    std::vector<PlotThread> plot_threads;
    std::vector<Chapter> chapters;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Outline, premise, plot_threads, chapters)
};

// 风格：写作风格配置。
struct Style {
    std::string tone = "neutral";             // atmospheric|dark|light|neutral|...
    std::string pacing = "moderate";          // slow|moderate|fast
    std::string pov = "third_person_limited"; // first_person|third_person_limited|third_person_omniscient
    std::string tense = "past";               // past|present
    std::string prose_style = "literary";     // literary|commercial|minimalist|descriptive
    std::string dialogue_style = "naturalistic"; // naturalistic|stylized|minimal
    std::string narrative_distance = "close"; // close|medium|distant
    int chapter_length_target = 4000;         // 目标章节字数
    std::string sentence_length = "varied";   // short|medium|long|varied
    std::string vocabulary = "rich";          // simple|moderate|rich
    std::string notes;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Style,
        tone, pacing, pov, tense, prose_style, dialogue_style,
        narrative_distance, chapter_length_target, sentence_length,
        vocabulary, notes)
};

// 项目：小说顶层元数据。
struct Project {
    int format_version = 1;                   // 结构版本号，便于后续兼容
    std::string title;
    std::string author;
    std::string description;
    std::vector<std::string> genre;
    int target_word_count = 0;
    int current_word_count = 0;
    std::string status = "planning";          // planning|in_progress|completed|on_hold
    std::string pov = "third_person_limited";
    std::string tense = "past";
    std::string created;                      // ISO 8601 时间戳
    std::string modified;                     // ISO 8601 时间戳

    // 运行期字段，不参与序列化，由文件系统位置推导得到。
    std::string path;

    // 子对象分别从独立 JSON 文件加载，不直接内嵌在 novel.json 中。
    Outline outline;
    std::vector<Character> characters;
    std::vector<Setting> settings;
    Style style;
};

// Project 的手写 JSON 序列化逻辑，排除仅运行期使用的 path 字段。
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
