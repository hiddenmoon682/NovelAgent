#pragma once

// NovelAgent 的核心数据模型。
//
// 设计目标：
// 1. 为小说写作提供足够强的结构化上下文，方便 LLM 理解人物、剧情、世界规则和文风。
// 2. 保留 tags / metadata 作为柔性扩展，避免频繁改 schema。
// 3. 通过 generation 控制每个对象或字段是否参与提示词组装，避免“信息越多越好”的僵硬约束。
// 4. 当前项目仍处于开发阶段，因此只保留低成本容错，不主动维护复杂的旧版本兼容分支。
#include "utils/JsonUtils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace project::model_detail {

using json = nlohmann::json;
using JsonMap = std::map<std::string, json>;

// 读取 metadata，同时把未知字段一并吸收到 metadata 中。
inline JsonMap getMetadataWithUnknownKeys(const json& j, const std::set<std::string>& knownKeys) {
    JsonMap metadata = utils::json::getOrDefault<JsonMap>(j, "metadata", {});
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!knownKeys.count(it.key())) {
            metadata[it.key()] = it.value();
        }
    }
    return metadata;
}

inline bool contains(const std::vector<std::string>& values, const std::string& target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

inline bool hasAnyTag(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    for (const auto& value : left) {
        if (contains(right, value)) {
            return true;
        }
    }
    return false;
}

} // namespace project::model_detail

// generation：控制对象或字段是否参与 LLM 提示词组装。
//
// 使用建议：
// - enabled=false：整个对象默认不参与生成。
// - include_fields 非空：只允许白名单字段进入提示词。
// - exclude_fields：即使字段存在，也不要喂给模型。
// - required_tags / blocked_tags：供上层在“按章节挑选上下文”时做筛选。
// - prompt_hint：留给提示词组装器的自然语言说明。
struct GenerationControl {
    bool enabled = true;
    std::vector<std::string> include_fields;
    std::vector<std::string> exclude_fields;
    std::vector<std::string> required_tags;
    std::vector<std::string> blocked_tags;
    std::string prompt_hint;
};

inline void to_json(nlohmann::json& j, const GenerationControl& g) {
    j = nlohmann::json{
        {"enabled", g.enabled},
        {"include_fields", g.include_fields},
        {"exclude_fields", g.exclude_fields},
        {"required_tags", g.required_tags},
        {"blocked_tags", g.blocked_tags},
        {"prompt_hint", g.prompt_hint}
    };
}

inline void from_json(const nlohmann::json& j, GenerationControl& g) {
    g.enabled = utils::json::getOrDefault(j, "enabled", true);
    g.include_fields = utils::json::getOrDefault(j, "include_fields", std::vector<std::string>{});
    g.exclude_fields = utils::json::getOrDefault(j, "exclude_fields", std::vector<std::string>{});
    g.required_tags = utils::json::getOrDefault(j, "required_tags", std::vector<std::string>{});
    g.blocked_tags = utils::json::getOrDefault(j, "blocked_tags", std::vector<std::string>{});
    g.prompt_hint = utils::json::getOrDefault(j, "prompt_hint", std::string{});
}

// 供上层组装提示词时直接调用的字段判定逻辑。
inline bool shouldUseField(
    const GenerationControl& generation,
    const std::string& fieldName,
    const std::vector<std::string>& objectTags = {}) {
    using namespace project::model_detail;

    if (!generation.enabled) {
        return false;
    }
    if (!generation.required_tags.empty() && !hasAnyTag(generation.required_tags, objectTags)) {
        return false;
    }
    if (!generation.blocked_tags.empty() && hasAnyTag(generation.blocked_tags, objectTags)) {
        return false;
    }
    if (!generation.include_fields.empty() && !contains(generation.include_fields, fieldName)) {
        return false;
    }
    if (contains(generation.exclude_fields, fieldName)) {
        return false;
    }
    return true;
}

// 场景：章节内部的最小戏剧单元。
struct Scene {
    std::string id;
    std::string title;
    std::string summary;
    std::string goal;
    std::string conflict;
    std::string outcome;
    std::string turning_point;
    std::string emotional_beat;
    std::string reveal;
    std::string foreshadowing;
    std::string payoff;
    std::string pov_character_id;
    std::string location_id;
    std::string time_marker;
    std::vector<std::string> participants;
    std::vector<std::string> plot_thread_ids;
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

// 角色关系：显式区分关系类型、公开状态和私下情绪。
struct Relationship {
    std::string target_character_id;
    std::string type;
    std::string description;
    std::string public_status;
    std::string private_feeling;
    std::string status = "active"; // active|strained|broken|resolved
    int tension = 0;               // 0-10，供提示词控制戏剧张力
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

// 世界规则：适合奇幻、科幻、悬疑等需要“规则一致性”的小说。
struct WorldRule {
    std::string id;
    std::string name;
    std::string summary;
    std::string limitations;
    std::string costs;
    std::string exceptions;
    std::string known_by; // everyone|experts|protagonist_only|hidden
    std::vector<std::string> related_settings;
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

// 章节：大纲中的单个条目。
struct Chapter {
    std::string id;                           // 例如 "ch-001"
    std::string title;                        // 例如 "发现"
    int order = 0;                            // 在整部小说中的顺序
    std::string synopsis;                     // 本章摘要
    std::string goal;                         // 本章主目标
    std::string conflict;                     // 本章核心冲突
    std::string outcome;                      // 结果：成功/失败/部分成功
    std::string turning_point;                // 关键转折
    std::string hook;                         // 章末钩子
    std::string reveal;                       // 本章揭示的信息
    std::string foreshadowing;                // 本章埋下的伏笔
    std::string payoff;                       // 本章回收的伏笔
    std::string emotional_beat;               // 情绪变化摘要
    std::string location_id;                  // 主要地点 ID
    std::string time_marker;                  // 时间标记，例如 "第一夜"
    std::vector<Scene> scenes;                // 场景列表
    std::vector<std::string> pov_characters;  // 作为 POV 的角色 ID
    std::vector<std::string> key_events;      // 推进剧情的关键事件
    std::vector<std::string> themes;          // 例如 "救赎"、"背叛"
    std::vector<std::string> active_plot_threads; // 本章推进的剧情线 ID
    std::vector<std::string> focus_characters;    // 本章重点角色 ID
    std::vector<std::string> focus_settings;      // 本章重点设定/地点/组织 ID
    std::string status = "outlined";          // outlined|drafting|drafted|revised|final
    int word_count = 0;
    std::string file_path;                    // 例如 chapters/001-title.md
    std::vector<std::string> tags;            // 轻量分类标签，如 "act-1"、"hook"
    GenerationControl generation;             // 控制本章哪些字段喂给 LLM
    std::map<std::string, nlohmann::json> metadata; // 章节扩展信息
};

// 角色：故事中的人物。
struct Character {
    std::string id;
    std::string name;
    std::string role = "supporting";          // protagonist|antagonist|supporting|minor
    std::string age;                          // 例如 "28"
    std::string appearance;                   // 外貌描述
    std::string personality;                  // 性格与习惯
    std::string background;                   // 背景经历
    std::string goal;                         // 当前最想达成的目标
    std::string motivation;                   // 目标背后的动机
    std::string internal_conflict;            // 内在冲突
    std::string external_conflict;            // 外在冲突
    std::string secret;                       // 暂不应轻易暴露的秘密
    std::string fear;                         // 核心恐惧
    std::string misbelief;                    // 角色的错误信念
    std::string speaking_style;               // 说话风格
    std::vector<std::string> traits;          // 例如 ["brave", "impulsive"]
    std::vector<std::string> core_values;     // 价值观，例如 ["loyalty"]
    std::vector<std::string> taboos;          // 不会做或不能做的事
    std::vector<Relationship> relationships;  // 结构化关系
    std::vector<std::string> chapter_appearances; // 出现过的章节 ID
    std::string arc;                          // 角色弧光摘要
    std::string notes;                        // 自由补充备注
    std::vector<std::string> tags;            // 轻量分类标签，如 "core-cast"
    GenerationControl generation;             // 控制角色字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 角色扩展信息
};

// 设定：世界观中的地点、组织、物品或规则载体。
struct Setting {
    std::string id;
    std::string name;                         // 例如 "Thorne University"
    std::string category = "location";        // location|organization|item|rule|other
    std::string description;                  // 自由描述
    std::string story_function;               // 叙事功能，例如 "safe-haven"
    std::string sensory_profile;              // 感官印象
    std::vector<std::string> related_characters;
    std::vector<std::string> related_plot_threads;
    std::vector<std::string> related_rule_ids;
    std::string notes;
    std::vector<std::string> tags;            // 轻量分类标签，如 "campus"、"ancient"
    GenerationControl generation;             // 控制设定字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 设定扩展信息
};

// 剧情线：主线或支线叙事线索。
struct PlotThread {
    std::string id;
    std::string name;                         // 例如 "主线任务"
    std::string description;
    std::string type = "main";               // main|subplot|romance|mystery|political|other
    std::string status = "planned";          // planned|active|paused|resolved
    int priority = 0;                         // 越高表示越需要被当前生成关注
    std::string stakes;                       // 失败的代价
    std::string central_question;             // 这条线最终要回答的问题
    std::string resolution;                   // 这条线的预期收束方式
    std::string start_chapter_id;
    std::string end_chapter_id;
    std::vector<std::string> related_characters;
    std::vector<std::string> related_settings;
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
};

// 大纲：完整的分层剧情结构。
struct Outline {
    std::string premise;                      // 一句话故事前提
    std::string story_structure;              // 例如 "three-act"
    std::vector<std::string> act_summaries;   // 各幕摘要
    std::vector<PlotThread> plot_threads;
    std::vector<Chapter> chapters;
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;
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
    std::string voice_reference;              // 叙事声音参考
    std::string show_vs_tell_bias = "balanced"; // show|balanced|tell
    std::string dialogue_density = "moderate";   // sparse|moderate|dense
    std::string description_density = "moderate"; // sparse|moderate|dense
    std::string introspection_density = "moderate"; // sparse|moderate|dense
    std::string humor_level = "low";          // none|low|moderate|high
    std::string sensory_focus;                // 例如 "visual, tactile"
    std::vector<std::string> forbidden_phrases;
    std::vector<std::string> forbidden_tropes;
    std::string chapter_opening_style;
    std::string chapter_ending_style;
    std::string notes;
    std::vector<std::string> tags;            // 轻量分类标签，如 "moody"
    GenerationControl generation;             // 控制风格字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 风格扩展信息
};

// 项目：小说顶层元数据。
struct Project {
    int format_version = 3;                   // 结构版本号，便于后续兼容
    std::string title;
    std::string author;
    std::string description;
    std::string logline;                      // 一句话卖点
    std::string theme;                        // 主题
    std::string central_question;             // 全书要回答的问题
    std::string target_audience;              // 目标读者
    std::vector<std::string> genre;
    std::vector<std::string> comps;           // 参考作品
    std::string content_rating;               // 例如 "PG-13"
    std::vector<std::string> must_have_elements;
    std::vector<std::string> must_avoid_elements;
    std::vector<std::string> narrative_promises; // 对读者的体验承诺
    std::string world_rules_summary;          // 世界规则总述
    std::string ending_type;                  // tragic|bittersweet|happy|open|twist
    int target_word_count = 0;
    int current_word_count = 0;
    std::string status = "planning";          // planning|in_progress|completed|on_hold
    std::string created;                      // ISO 8601 时间戳
    std::string modified;                     // ISO 8601 时间戳
    std::vector<std::string> tags;            // 项目级标签，如题材、优先级、阶段分组
    GenerationControl generation;             // 项目级提示词控制
    std::map<std::string, nlohmann::json> metadata; // 项目级扩展信息

    // 运行期字段，不参与序列化，由文件系统位置推导得到。
    std::string path;

    // 子对象分别从独立 JSON 文件加载，不直接内嵌在 novel.json 中。
    Outline outline;
    std::vector<Character> characters;
    std::vector<Setting> settings;
    std::vector<WorldRule> world_rules;
    Style style;
};

inline void to_json(nlohmann::json& j, const Scene& s) {
    j = nlohmann::json{
        {"id", s.id},
        {"title", s.title},
        {"summary", s.summary},
        {"goal", s.goal},
        {"conflict", s.conflict},
        {"outcome", s.outcome},
        {"turning_point", s.turning_point},
        {"emotional_beat", s.emotional_beat},
        {"reveal", s.reveal},
        {"foreshadowing", s.foreshadowing},
        {"payoff", s.payoff},
        {"pov_character_id", s.pov_character_id},
        {"location_id", s.location_id},
        {"time_marker", s.time_marker},
        {"participants", s.participants},
        {"plot_thread_ids", s.plot_thread_ids},
        {"tags", s.tags},
        {"generation", s.generation},
        {"metadata", s.metadata}
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

inline void to_json(nlohmann::json& j, const Relationship& r) {
    j = nlohmann::json{
        {"target_character_id", r.target_character_id},
        {"type", r.type},
        {"description", r.description},
        {"public_status", r.public_status},
        {"private_feeling", r.private_feeling},
        {"status", r.status},
        {"tension", r.tension},
        {"tags", r.tags},
        {"generation", r.generation},
        {"metadata", r.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Relationship& r) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "target_character_id", "type", "description", "public_status",
        "private_feeling", "status", "tension", "tags", "generation", "metadata"
    };

    r.target_character_id = utils::json::getOrDefault(j, "target_character_id", std::string{});
    r.type = utils::json::getOrDefault(j, "type", std::string{});
    r.description = utils::json::getOrDefault(j, "description", std::string{});
    r.public_status = utils::json::getOrDefault(j, "public_status", std::string{});
    r.private_feeling = utils::json::getOrDefault(j, "private_feeling", std::string{});
    r.status = utils::json::getOrDefault(j, "status", std::string{"active"});
    r.tension = utils::json::getOrDefault(j, "tension", 0);
    r.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    r.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    r.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

inline void to_json(nlohmann::json& j, const WorldRule& r) {
    j = nlohmann::json{
        {"id", r.id},
        {"name", r.name},
        {"summary", r.summary},
        {"limitations", r.limitations},
        {"costs", r.costs},
        {"exceptions", r.exceptions},
        {"known_by", r.known_by},
        {"related_settings", r.related_settings},
        {"tags", r.tags},
        {"generation", r.generation},
        {"metadata", r.metadata}
    };
}

inline void from_json(const nlohmann::json& j, WorldRule& r) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "summary", "limitations", "costs", "exceptions",
        "known_by", "related_settings", "tags", "generation", "metadata"
    };

    r.id = utils::json::getOrDefault(j, "id", std::string{});
    r.name = utils::json::getOrDefault(j, "name", std::string{});
    r.summary = utils::json::getOrDefault(j, "summary", std::string{});
    r.limitations = utils::json::getOrDefault(j, "limitations", std::string{});
    r.costs = utils::json::getOrDefault(j, "costs", std::string{});
    r.exceptions = utils::json::getOrDefault(j, "exceptions", std::string{});
    r.known_by = utils::json::getOrDefault(j, "known_by", std::string{});
    r.related_settings = utils::json::getOrDefault(j, "related_settings", std::vector<std::string>{});
    r.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    r.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    r.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

inline void to_json(nlohmann::json& j, const Chapter& c) {
    j = nlohmann::json{
        {"id", c.id},
        {"title", c.title},
        {"order", c.order},
        {"synopsis", c.synopsis},
        {"goal", c.goal},
        {"conflict", c.conflict},
        {"outcome", c.outcome},
        {"turning_point", c.turning_point},
        {"hook", c.hook},
        {"reveal", c.reveal},
        {"foreshadowing", c.foreshadowing},
        {"payoff", c.payoff},
        {"emotional_beat", c.emotional_beat},
        {"location_id", c.location_id},
        {"time_marker", c.time_marker},
        {"scenes", c.scenes},
        {"pov_characters", c.pov_characters},
        {"key_events", c.key_events},
        {"themes", c.themes},
        {"active_plot_threads", c.active_plot_threads},
        {"focus_characters", c.focus_characters},
        {"focus_settings", c.focus_settings},
        {"status", c.status},
        {"word_count", c.word_count},
        {"file_path", c.file_path},
        {"tags", c.tags},
        {"generation", c.generation},
        {"metadata", c.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Chapter& c) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "title", "order", "synopsis", "goal", "conflict", "outcome",
        "turning_point", "hook", "reveal", "foreshadowing", "payoff",
        "emotional_beat", "location_id", "time_marker", "scenes",
        "pov_characters", "key_events", "themes", "active_plot_threads",
        "focus_characters", "focus_settings", "status", "word_count",
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
    c.status = utils::json::getOrDefault(j, "status", std::string{"outlined"});
    c.word_count = utils::json::getOrDefault(j, "word_count", 0);
    c.file_path = utils::json::getOrDefault(j, "file_path", std::string{});
    c.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    c.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
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
        {"goal", c.goal},
        {"motivation", c.motivation},
        {"internal_conflict", c.internal_conflict},
        {"external_conflict", c.external_conflict},
        {"secret", c.secret},
        {"fear", c.fear},
        {"misbelief", c.misbelief},
        {"speaking_style", c.speaking_style},
        {"traits", c.traits},
        {"core_values", c.core_values},
        {"taboos", c.taboos},
        {"relationships", c.relationships},
        {"chapter_appearances", c.chapter_appearances},
        {"arc", c.arc},
        {"notes", c.notes},
        {"tags", c.tags},
        {"generation", c.generation},
        {"metadata", c.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Character& c) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "role", "age", "appearance", "personality",
        "background", "goal", "motivation", "internal_conflict",
        "external_conflict", "secret", "fear", "misbelief",
        "speaking_style", "traits", "core_values", "taboos",
        "relationships", "chapter_appearances", "arc", "notes",
        "tags", "generation", "metadata"
    };

    c.id = utils::json::getOrDefault(j, "id", std::string{});
    c.name = utils::json::getOrDefault(j, "name", std::string{});
    c.role = utils::json::getOrDefault(j, "role", std::string{"supporting"});
    c.age = utils::json::getOrDefault(j, "age", std::string{});
    c.appearance = utils::json::getOrDefault(j, "appearance", std::string{});
    c.personality = utils::json::getOrDefault(j, "personality", std::string{});
    c.background = utils::json::getOrDefault(j, "background", std::string{});
    c.goal = utils::json::getOrDefault(j, "goal", std::string{});
    c.motivation = utils::json::getOrDefault(j, "motivation", std::string{});
    c.internal_conflict = utils::json::getOrDefault(j, "internal_conflict", std::string{});
    c.external_conflict = utils::json::getOrDefault(j, "external_conflict", std::string{});
    c.secret = utils::json::getOrDefault(j, "secret", std::string{});
    c.fear = utils::json::getOrDefault(j, "fear", std::string{});
    c.misbelief = utils::json::getOrDefault(j, "misbelief", std::string{});
    c.speaking_style = utils::json::getOrDefault(j, "speaking_style", std::string{});
    c.traits = utils::json::getOrDefault(j, "traits", std::vector<std::string>{});
    c.core_values = utils::json::getOrDefault(j, "core_values", std::vector<std::string>{});
    c.taboos = utils::json::getOrDefault(j, "taboos", std::vector<std::string>{});

    c.relationships = utils::json::getOrDefault(j, "relationships", std::vector<Relationship>{});

    c.chapter_appearances = utils::json::getOrDefault(j, "chapter_appearances", std::vector<std::string>{});
    c.arc = utils::json::getOrDefault(j, "arc", std::string{});
    c.notes = utils::json::getOrDefault(j, "notes", std::string{});
    c.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    c.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

inline void to_json(nlohmann::json& j, const Setting& s) {
    j = nlohmann::json{
        {"id", s.id},
        {"name", s.name},
        {"category", s.category},
        {"description", s.description},
        {"story_function", s.story_function},
        {"sensory_profile", s.sensory_profile},
        {"related_characters", s.related_characters},
        {"related_plot_threads", s.related_plot_threads},
        {"related_rule_ids", s.related_rule_ids},
        {"notes", s.notes},
        {"tags", s.tags},
        {"generation", s.generation},
        {"metadata", s.metadata}
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

inline void to_json(nlohmann::json& j, const PlotThread& p) {
    j = nlohmann::json{
        {"id", p.id},
        {"name", p.name},
        {"description", p.description},
        {"type", p.type},
        {"status", p.status},
        {"priority", p.priority},
        {"stakes", p.stakes},
        {"central_question", p.central_question},
        {"resolution", p.resolution},
        {"start_chapter_id", p.start_chapter_id},
        {"end_chapter_id", p.end_chapter_id},
        {"related_characters", p.related_characters},
        {"related_settings", p.related_settings},
        {"tags", p.tags},
        {"generation", p.generation},
        {"metadata", p.metadata}
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

inline void to_json(nlohmann::json& j, const Outline& o) {
    j = nlohmann::json{
        {"premise", o.premise},
        {"story_structure", o.story_structure},
        {"act_summaries", o.act_summaries},
        {"plot_threads", o.plot_threads},
        {"chapters", o.chapters},
        {"tags", o.tags},
        {"generation", o.generation},
        {"metadata", o.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Outline& o) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "premise", "story_structure", "act_summaries", "plot_threads",
        "chapters", "tags", "generation", "metadata"
    };

    o.premise = utils::json::getOrDefault(j, "premise", std::string{});
    o.story_structure = utils::json::getOrDefault(j, "story_structure", std::string{});
    o.act_summaries = utils::json::getOrDefault(j, "act_summaries", std::vector<std::string>{});
    o.plot_threads = utils::json::getOrDefault(j, "plot_threads", std::vector<PlotThread>{});
    o.chapters = utils::json::getOrDefault(j, "chapters", std::vector<Chapter>{});
    o.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    o.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    o.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
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
        {"voice_reference", s.voice_reference},
        {"show_vs_tell_bias", s.show_vs_tell_bias},
        {"dialogue_density", s.dialogue_density},
        {"description_density", s.description_density},
        {"introspection_density", s.introspection_density},
        {"humor_level", s.humor_level},
        {"sensory_focus", s.sensory_focus},
        {"forbidden_phrases", s.forbidden_phrases},
        {"forbidden_tropes", s.forbidden_tropes},
        {"chapter_opening_style", s.chapter_opening_style},
        {"chapter_ending_style", s.chapter_ending_style},
        {"notes", s.notes},
        {"tags", s.tags},
        {"generation", s.generation},
        {"metadata", s.metadata}
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
        "notes", "tags", "generation", "metadata"
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
    s.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    s.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}

inline void to_json(nlohmann::json& j, const Project& p) {
    j = nlohmann::json{
        {"format_version", p.format_version},
        {"title", p.title},
        {"author", p.author},
        {"description", p.description},
        {"logline", p.logline},
        {"theme", p.theme},
        {"central_question", p.central_question},
        {"target_audience", p.target_audience},
        {"genre", p.genre},
        {"comps", p.comps},
        {"content_rating", p.content_rating},
        {"must_have_elements", p.must_have_elements},
        {"must_avoid_elements", p.must_avoid_elements},
        {"narrative_promises", p.narrative_promises},
        {"world_rules_summary", p.world_rules_summary},
        {"ending_type", p.ending_type},
        {"target_word_count", p.target_word_count},
        {"current_word_count", p.current_word_count},
        {"status", p.status},
        {"created", p.created},
        {"modified", p.modified},
        {"tags", p.tags},
        {"generation", p.generation},
        {"metadata", p.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Project& p) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "format_version", "title", "author", "description", "logline",
        "theme", "central_question", "target_audience", "genre", "comps",
        "content_rating", "must_have_elements", "must_avoid_elements",
        "narrative_promises", "world_rules_summary", "ending_type",
        "target_word_count", "current_word_count", "status", "created",
        "modified", "tags", "generation", "metadata"
    };

    p.format_version = utils::json::getOrDefault(j, "format_version", 1);
    p.title = utils::json::getOrDefault(j, "title", std::string{});
    p.author = utils::json::getOrDefault(j, "author", std::string{});
    p.description = utils::json::getOrDefault(j, "description", std::string{});
    p.logline = utils::json::getOrDefault(j, "logline", std::string{});
    p.theme = utils::json::getOrDefault(j, "theme", std::string{});
    p.central_question = utils::json::getOrDefault(j, "central_question", std::string{});
    p.target_audience = utils::json::getOrDefault(j, "target_audience", std::string{});
    p.genre = utils::json::getOrDefault(j, "genre", std::vector<std::string>{});
    p.comps = utils::json::getOrDefault(j, "comps", std::vector<std::string>{});
    p.content_rating = utils::json::getOrDefault(j, "content_rating", std::string{});
    p.must_have_elements = utils::json::getOrDefault(j, "must_have_elements", std::vector<std::string>{});
    p.must_avoid_elements = utils::json::getOrDefault(j, "must_avoid_elements", std::vector<std::string>{});
    p.narrative_promises = utils::json::getOrDefault(j, "narrative_promises", std::vector<std::string>{});
    p.world_rules_summary = utils::json::getOrDefault(j, "world_rules_summary", std::string{});
    p.ending_type = utils::json::getOrDefault(j, "ending_type", std::string{});
    p.target_word_count = utils::json::getOrDefault(j, "target_word_count", 0);
    p.current_word_count = utils::json::getOrDefault(j, "current_word_count", 0);
    p.status = utils::json::getOrDefault(j, "status", std::string{"planning"});
    p.created = utils::json::getOrDefault(j, "created", std::string{});
    p.modified = utils::json::getOrDefault(j, "modified", std::string{});
    p.tags = utils::json::getOrDefault(j, "tags", std::vector<std::string>{});
    p.generation = utils::json::getOrDefault(j, "generation", GenerationControl{});
    p.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
