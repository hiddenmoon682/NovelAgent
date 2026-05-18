#pragma once

// NovelAgent 的核心数据模型。
// 采用“稳定核心字段 + 半结构化扩展字段”的方式：
//   - 常用、稳定、跨模块通用的信息保留为强类型字段
//   - tags 用于轻量分类
//   - metadata 用于容纳未来的扩展创作元数据
// 同时保持对旧 JSON 的兼容加载，缺字段时自动回落默认值。

#include "utils/JsonUtils.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace project::model_detail {

using json = nlohmann::json;
using JsonMap = std::map<std::string, json>;

// 读取 metadata，同时把未知字段一并吸收到 metadata 中。
// 这样旧数据里的临时字段、未来版本里的新增字段都不会在加载时丢失。
inline JsonMap getMetadataWithUnknownKeys(const json& j, const std::set<std::string>& knownKeys) {
    JsonMap metadata = utils::json::getOrDefault<JsonMap>(j, "metadata", {});
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!knownKeys.count(it.key())) {
            metadata[it.key()] = it.value();
        }
    }
    return metadata;
}

// 把旧版 string -> string 结构合并进 metadata。
// 仅在 metadata 中不存在同名键时写入，避免覆盖新结构。
inline void mergeStringMapIntoMetadata(
    JsonMap& metadata,
    const std::map<std::string, std::string>& values) {
    for (const auto& [key, value] : values) {
        if (!metadata.contains(key)) {
            metadata[key] = value;
        }
    }
}

// 从 metadata 中提取纯字符串键值对，供旧版 attributes 等兼容字段回填。
inline std::map<std::string, std::string> stringMapFromJsonValues(const JsonMap& metadata) {
    std::map<std::string, std::string> result;
    for (const auto& [key, value] : metadata) {
        if (value.is_string()) {
            result[key] = value.get<std::string>();
        }
    }
    return result;
}

} // namespace project::model_detail

// 章节：大纲中的单个条目。
struct Chapter {
    std::string id;                           // 例如 "ch-001"
    std::string title;                        // 例如 "发现"
    int order = 0;                            // 在整部小说中的顺序
    std::string synopsis;                     // 1 到 2 句摘要
    std::vector<std::string> scenes;          // 按顺序排列的场景描述
    std::vector<std::string> pov_characters;  // 作为 POV 的角色 ID
    std::vector<std::string> key_events;      // 推进剧情的关键事件
    std::vector<std::string> themes;          // 例如 "救赎"、"背叛"
    std::string status = "outlined";          // outlined|drafting|drafted|revised|final
    int word_count = 0;
    std::string file_path;                    // 例如 chapters/001-title.md
    std::vector<std::string> tags;            // 轻量分类标签，如 "act-1"、"hook"
    std::map<std::string, nlohmann::json> metadata; // 章节扩展信息，如节奏目标、情绪曲线、伏笔列表
};

// 角色：故事中的人物。
struct Character {
    std::string id;
    std::string name;
    std::string role = "supporting";          // protagonist|antagonist|supporting|minor
    std::string age;                          // 例如 "28" 或 "二十多岁末"
    std::string appearance;                   // 外貌描述
    std::string personality;                  // 性格与习惯
    std::string background;                   // 背景经历
    std::vector<std::string> traits;          // 例如 ["brave", "impulsive"]
    std::map<std::string, std::string> relationships; // 角色 ID -> 关系描述
    std::vector<std::string> chapter_appearances;     // 出现过的章节 ID
    std::string arc;                          // 角色弧光摘要
    std::string notes;                        // 自由补充备注
    std::vector<std::string> tags;            // 轻量分类标签，如 "core-cast"
    std::map<std::string, nlohmann::json> metadata; // 角色扩展信息，如秘密、动机、能力、禁忌
};

// 设定：世界观中的地点、组织、物品或规则。
struct Setting {
    std::string id;
    std::string name;                         // 例如 "Thorne University"
    std::string category = "location";        // location|organization|item|rule|other
    std::string description;                  // 自由描述
    std::map<std::string, std::string> attributes; // 兼容旧版的结构化键值对
    std::string notes;
    std::vector<std::string> tags;            // 轻量分类标签，如 "campus"、"ancient"
    std::map<std::string, nlohmann::json> metadata; // 设定扩展信息；长期会逐步替代 attributes
};

// 剧情线：主线或支线叙事线程。
struct PlotThread {
    std::string id;
    std::string name;                         // 例如 "主线任务"、"感情线"
    std::string description;
};

// 大纲：完整的分层剧情结构。
struct Outline {
    std::string premise;                      // 一段式故事前提
    std::vector<PlotThread> plot_threads;
    std::vector<Chapter> chapters;
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
    std::vector<std::string> tags;            // 轻量分类标签，如 "moody"
    std::map<std::string, nlohmann::json> metadata; // 风格扩展信息，如禁用表达、示例约束、提示偏好
};

// 项目：小说顶层元数据。
struct Project {
    int format_version = 2;                   // 结构版本号，便于后续兼容
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
    std::vector<std::string> tags;            // 项目级标签，如题材、优先级、阶段分组
    std::map<std::string, nlohmann::json> metadata; // 项目级扩展信息，如工作流状态、全局约束

    // 运行期字段，不参与序列化，由文件系统位置推导得到。
    std::string path;

    // 子对象分别从独立 JSON 文件加载，不直接内嵌在 novel.json 中。
    Outline outline;
    std::vector<Character> characters;
    std::vector<Setting> settings;
    Style style;
};

inline void to_json(nlohmann::json& j, const Chapter& c) {
    j = nlohmann::json{
        {"id", c.id},
        {"title", c.title},
        {"order", c.order},
        {"synopsis", c.synopsis},
        {"scenes", c.scenes},
        {"pov_characters", c.pov_characters},
        {"key_events", c.key_events},
        {"themes", c.themes},
        {"status", c.status},
        {"word_count", c.word_count},
        {"file_path", c.file_path},
        {"tags", c.tags},
        {"metadata", c.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Chapter& c) {
    using namespace project::model_detail;
    // knownKeys 定义当前版本显式支持的核心字段。
    // 未列入的字段不会报错，而是自动并入 metadata。
    static const std::set<std::string> kKnownKeys = {
        "id", "title", "order", "synopsis", "scenes", "pov_characters",
        "key_events", "themes", "status", "word_count", "file_path",
        "tags", "metadata"
    };

    c.id = utils::json::getOrDefault(j, "id", std::string{});
    c.title = utils::json::getOrDefault(j, "title", std::string{});
    c.order = utils::json::getOrDefault(j, "order", 0);
    c.synopsis = utils::json::getOrDefault(j, "synopsis", std::string{});
    c.scenes = utils::json::getOrDefault(j, "scenes", std::vector<std::string>{});
    c.pov_characters = utils::json::getOrDefault(j, "pov_characters", std::vector<std::string>{});
    c.key_events = utils::json::getOrDefault(j, "key_events", std::vector<std::string>{});
    c.themes = utils::json::getOrDefault(j, "themes", std::vector<std::string>{});
    c.status = utils::json::getOrDefault(j, "status", std::string{"outlined"});
    c.word_count = utils::json::getOrDefault(j, "word_count", 0);
    c.file_path = utils::json::getOrDefault(j, "file_path", std::string{});
    c.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

inline void to_json(nlohmann::json& j, const Character& c) {
    j = nlohmann::json{
        {"id", c.id},
        {"name", c.name},
        {"role", c.role},
        {"age", c.age},
        {"appearance", c.appearance},
        {"personality", c.personality},
        {"background", c.background},
        {"traits", c.traits},
        {"relationships", c.relationships},
        {"chapter_appearances", c.chapter_appearances},
        {"arc", c.arc},
        {"notes", c.notes},
        {"tags", c.tags},
        {"metadata", c.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Character& c) {
    using namespace project::model_detail;
    // 角色信息增长很快，未知字段优先进入 metadata，减少频繁改模型的成本。
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "role", "age", "appearance", "personality",
        "background", "traits", "relationships", "chapter_appearances",
        "arc", "notes", "tags", "metadata"
    };

    c.id = utils::json::getOrDefault(j, "id", std::string{});
    c.name = utils::json::getOrDefault(j, "name", std::string{});
    c.role = utils::json::getOrDefault(j, "role", std::string{"supporting"});
    c.age = utils::json::getOrDefault(j, "age", std::string{});
    c.appearance = utils::json::getOrDefault(j, "appearance", std::string{});
    c.personality = utils::json::getOrDefault(j, "personality", std::string{});
    c.background = utils::json::getOrDefault(j, "background", std::string{});
    c.traits = utils::json::getOrDefault(j, "traits", std::vector<std::string>{});
    c.relationships = utils::json::getOrDefault(j, "relationships", std::map<std::string, std::string>{});
    c.chapter_appearances = utils::json::getOrDefault(j, "chapter_appearances", std::vector<std::string>{});
    c.arc = utils::json::getOrDefault(j, "arc", std::string{});
    c.notes = utils::json::getOrDefault(j, "notes", std::string{});
    c.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

inline void to_json(nlohmann::json& j, const Setting& s) {
    j = nlohmann::json{
        {"id", s.id},
        {"name", s.name},
        {"category", s.category},
        {"description", s.description},
        {"attributes", s.attributes},
        {"notes", s.notes},
        {"tags", s.tags},
        {"metadata", s.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Setting& s) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "category", "description", "attributes",
        "notes", "tags", "metadata"
    };

    s.id = utils::json::getOrDefault(j, "id", std::string{});
    s.name = utils::json::getOrDefault(j, "name", std::string{});
    s.category = utils::json::getOrDefault(j, "category", std::string{"location"});
    s.description = utils::json::getOrDefault(j, "description", std::string{});
    s.attributes = utils::json::getOrDefault(j, "attributes", std::map<std::string, std::string>{});
    s.notes = utils::json::getOrDefault(j, "notes", std::string{});
    s.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
    // 兼容旧版 attributes，同时让新逻辑统一从 metadata 读扩展字段。
    mergeStringMapIntoMetadata(s.metadata, s.attributes);
}

inline void to_json(nlohmann::json& j, const PlotThread& p) {
    j = nlohmann::json{
        {"id", p.id},
        {"name", p.name},
        {"description", p.description}
    };
}

inline void from_json(const nlohmann::json& j, PlotThread& p) {
    p.id = utils::json::getOrDefault(j, "id", std::string{});
    p.name = utils::json::getOrDefault(j, "name", std::string{});
    p.description = utils::json::getOrDefault(j, "description", std::string{});
}

inline void to_json(nlohmann::json& j, const Outline& o) {
    j = nlohmann::json{
        {"premise", o.premise},
        {"plot_threads", o.plot_threads},
        {"chapters", o.chapters}
    };
}

inline void from_json(const nlohmann::json& j, Outline& o) {
    o.premise = utils::json::getOrDefault(j, "premise", std::string{});
    o.plot_threads = utils::json::getOrDefault(j, "plot_threads", std::vector<PlotThread>{});
    o.chapters = utils::json::getOrDefault(j, "chapters", std::vector<Chapter>{});
}

inline void to_json(nlohmann::json& j, const Style& s) {
    j = nlohmann::json{
        {"tone", s.tone},
        {"pacing", s.pacing},
        {"pov", s.pov},
        {"tense", s.tense},
        {"prose_style", s.prose_style},
        {"dialogue_style", s.dialogue_style},
        {"narrative_distance", s.narrative_distance},
        {"chapter_length_target", s.chapter_length_target},
        {"sentence_length", s.sentence_length},
        {"vocabulary", s.vocabulary},
        {"notes", s.notes},
        {"tags", s.tags},
        {"metadata", s.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Style& s) {
    using namespace project::model_detail;
    // 风格配置未来很可能继续扩张，所以默认容纳未知字段。
    static const std::set<std::string> kKnownKeys = {
        "tone", "pacing", "pov", "tense", "prose_style", "dialogue_style",
        "narrative_distance", "chapter_length_target", "sentence_length",
        "vocabulary", "notes", "tags", "metadata"
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
    s.notes = utils::json::getOrDefault(j, "notes", std::string{});
    s.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

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
        {"modified", p.modified},
        {"tags", p.tags},
        {"metadata", p.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Project& p) {
    using namespace project::model_detail;
    // Project 只保留稳定的顶层元数据；其余顶层扩展字段统一落入 metadata。
    static const std::set<std::string> kKnownKeys = {
        "format_version", "title", "author", "description", "genre",
        "target_word_count", "current_word_count", "status", "pov",
        "tense", "created", "modified", "tags", "metadata"
    };

    p.format_version = utils::json::getOrDefault(j, "format_version", 1);
    p.title = utils::json::getOrDefault(j, "title", std::string{});
    p.author = utils::json::getOrDefault(j, "author", std::string{});
    p.description = utils::json::getOrDefault(j, "description", std::string{});
    p.genre = utils::json::getOrDefault(j, "genre", std::vector<std::string>{});
    p.target_word_count = utils::json::getOrDefault(j, "target_word_count", 0);
    p.current_word_count = utils::json::getOrDefault(j, "current_word_count", 0);
    p.status = utils::json::getOrDefault(j, "status", std::string{"planning"});
    p.pov = utils::json::getOrDefault(j, "pov", std::string{"third_person_limited"});
    p.tense = utils::json::getOrDefault(j, "tense", std::string{"past"});
    p.created = utils::json::getOrDefault(j, "created", std::string{});
    p.modified = utils::json::getOrDefault(j, "modified", std::string{});
    p.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    p.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
