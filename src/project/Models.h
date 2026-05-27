#pragma once

// NovelAgent 的核心数据模型。
//
// 设计目标：
// 1. 为小说写作提供足够强的结构化上下文，方便 LLM 理解人物、剧情、世界规则和文风。
// 2. 保留 tags / metadata 作为柔性扩展，避免频繁改 schema。
// 3. 通过 generation 控制每个对象或字段是否参与提示词组装，避免"信息越多越好"的僵硬约束。
// 4. 当前项目仍处于开发阶段，因此只保留低成本容错，不主动维护复杂的旧版本兼容分支。
//
// 结构层次:
//   Project
//   ├── Outline
//   │   ├── PlotThread[]
//   │   └── Chapter[]
//   │       └── Scene[]
//   ├── Character[]
//   │   └── Relationship[]
//   ├── Setting[]
//   ├── WorldRule[]
//   └── Style

#include "utils/JsonUtils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// ──────────────────────────────────────────────
//  内部辅助函数（project::model_detail 命名空间）
// ──────────────────────────────────────────────

namespace project::model_detail {

using json = nlohmann::json;                    // JSON 对象类型别名，简化代码书写。
using JsonMap = std::map<std::string, json>;    // JSON 对象的别名，表示一组键值对。

// 读取 metadata 字段，同时将 JSON 中的未知字段一并吸收到 metadata 中。
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

// 判断向量中是否包含目标字符串。
inline bool contains(const std::vector<std::string>& values, const std::string& target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

// 判断两个标签列表是否有交集，用于 GenerationControl 的标签过滤。
inline bool hasAnyTag(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    for (const auto& value : left) {
        if (contains(right, value)) {
            return true;
        }
    }
    return false;
}

} // namespace project::model_detail

// ──────────────────────────────────────────────
//  GenerationControl — 提示词组装控制器
// ──────────────────────────────────────────────

// generation：控制对象或其字段是否参与 LLM 提示词组装。
// 每个核心 struct 都内嵌一个 GenerationControl 实例。
//
// 使用建议：
// - enabled=false：整个对象默认不参与生成。
// - include_fields 非空：只允许白名单字段进入提示词。
// - exclude_fields：即使字段存在，也不要喂给模型。
// - required_tags / blocked_tags：供上层在"按章节挑选上下文"时做筛选。
// - prompt_hint：留给提示词组装器的自然语言说明。
struct GenerationControl {
    bool enabled = true;                        // 是否整体启用
    std::vector<std::string> include_fields;    // 白名单：仅允许这些字段进入提示词
    std::vector<std::string> exclude_fields;    // 黑名单：禁止这些字段进入提示词
    std::vector<std::string> required_tags;     // 对象必须具备的标签才会被选中
    std::vector<std::string> blocked_tags;      // 对象如果带有这些标签则排除
    std::string prompt_hint;                    // 供 PromptContextBuilder 使用的自然语言提示
};

// GenerationControl 的 JSON 序列化。
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

// GenerationControl 的 JSON 反序列化，缺失字段回落默认值。
inline void from_json(const nlohmann::json& j, GenerationControl& g) {
    g.enabled = utils::json::getOrDefault(j, "enabled", true);
    g.include_fields = utils::json::getOrDefault(j, "include_fields", std::vector<std::string>{});
    g.exclude_fields = utils::json::getOrDefault(j, "exclude_fields", std::vector<std::string>{});
    g.required_tags = utils::json::getOrDefault(j, "required_tags", std::vector<std::string>{});
    g.blocked_tags = utils::json::getOrDefault(j, "blocked_tags", std::vector<std::string>{});
    g.prompt_hint = utils::json::getOrDefault(j, "prompt_hint", std::string{});
}

// 判定一个字段是否应该进入 LLM 提示词。
// 检查顺序：enabled → required_tags → blocked_tags → include_fields → exclude_fields
// objectTags 是对象自身的标签列表，用于 required/blocked 标签匹配。
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

// ──────────────────────────────────────────────
//  Scene — 章节内部的最小戏剧单元
// ──────────────────────────────────────────────

struct Scene {
    std::string id;                             // 场景唯一标识
    std::string title;                          // 场景标题
    std::string summary;                        // 场景概要
    std::string goal;                           // 本场景要达成的目的
    std::string conflict;                       // 场景内的核心冲突
    std::string outcome;                        // 场景结果：成功/失败/转折
    std::string turning_point;                  // 场景内的关键转折点
    std::string emotional_beat;                 // 场景情绪基调
    std::string reveal;                         // 本场景揭示的新信息
    std::string foreshadowing;                  // 本场景埋下的伏笔
    std::string payoff;                         // 本场景回收的伏笔
    std::string pov_character_id;               // 本场景的 POV 角色 ID
    std::string location_id;                    // 发生地点 ID
    std::string time_marker;                    // 时间标记，例如 "黄昏"
    std::vector<std::string> participants;      // 出场角色 ID 列表
    std::vector<std::string> plot_thread_ids;   // 关联的剧情线 ID
    std::vector<std::string> tags;              // 轻量分类标签，如 "quiet"
    GenerationControl generation;               // 控制场景字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 场景扩展信息
};

// Scene 的 JSON 序列化。
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

// Scene 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  Relationship — 角色之间的关系
// ──────────────────────────────────────────────

struct Relationship {
    std::string target_character_id;            // 对方角色 ID
    std::string type;                           // 关系类型，例如 "mentor"、"rival"
    std::string description;                    // 关系描述
    std::string public_status;                  // 公开场合的关系状态
    std::string private_feeling;                // 私下的真实感受
    std::string status = "active";              // active|strained|broken|resolved
    int tension = 0;                            // 戏剧张力 0-10
    std::vector<std::string> tags;              // 轻量分类标签
    GenerationControl generation;               // 控制关系字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 关系扩展信息
};

// Relationship 的 JSON 序列化。
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

// Relationship 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  WorldRule — 世界规则
// ──────────────────────────────────────────────

// 适合奇幻、科幻、悬疑等需要"规则一致性"的小说。
struct WorldRule {
    std::string id;                             // 规则唯一标识
    std::string name;                           // 规则名称
    std::string summary;                        // 规则概要
    std::string limitations;                    // 规则的限制条件
    std::string costs;                          // 使用该规则的代价
    std::string exceptions;                     // 规则的例外情况
    std::string known_by;                       // 知晓范围：everyone|experts|protagonist_only|hidden
    std::vector<std::string> related_settings;  // 关联的设定 ID
    std::vector<std::string> tags;              // 轻量分类标签
    GenerationControl generation;               // 控制规则字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 规则扩展信息
};

// WorldRule 的 JSON 序列化。
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

// WorldRule 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  Chapter — 章节
// ──────────────────────────────────────────────

struct Chapter {
    std::string id;                             // 章节唯一标识，例如 "ch-001"
    std::string title;                          // 章节标题，例如 "发现"
    int order = 0;                              // 在整部小说中的顺序编号
    std::string synopsis;                       // 1 到 2 句章节摘要
    std::string goal;                           // 本章主目标
    std::string conflict;                       // 本章核心冲突
    std::string outcome;                        // 本章结果：成功/失败/部分成功
    std::string turning_point;                  // 本章关键转折
    std::string hook;                           // 章末钩子，吸引读者继续阅读
    std::string reveal;                         // 本章向读者揭示的新信息
    std::string foreshadowing;                  // 本章埋下的伏笔
    std::string payoff;                         // 本章回收的伏笔（来自前文）
    std::string emotional_beat;                 // 本章情绪变化摘要
    std::string location_id;                    // 本章主要发生地点的 ID
    std::string time_marker;                    // 时间标记，例如 "第三夜"
    std::vector<Scene> scenes;                  // 章节内的场景列表
    std::vector<std::string> pov_characters;    // 本章的 POV 角色 ID 列表
    std::vector<std::string> key_events;        // 本章推进剧情的关键事件
    std::vector<std::string> themes;            // 本章主题，例如 "救赎"、"背叛"
    std::vector<std::string> active_plot_threads; // 本章推动的剧情线 ID
    std::vector<std::string> focus_characters;    // 本章重点角色 ID
    std::vector<std::string> focus_settings;      // 本章重点设定/地点/组织 ID
    std::string status = "outlined";            // outlined|drafting|drafted|revised|final
    int word_count = 0;                         // 当前字数统计
    std::string file_path;                      // 章节 Markdown 文件路径，例如 chapters/001-title.md
    std::vector<std::string> tags;              // 轻量分类标签，如 "act-1"、"hook"
    GenerationControl generation;               // 控制章节字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 章节扩展信息
};

// Chapter 的 JSON 序列化。
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

// Chapter 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
inline void from_json(const nlohmann::json& j, Chapter& c) {
    using namespace project::model_detail;
    // kKnownKeys 定义当前版本显式支持的核心字段。
    // 未列入的字段不会报错，而是自动并入 metadata。
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

// ──────────────────────────────────────────────
//  Character — 角色
// ──────────────────────────────────────────────

struct Character {
    std::string id;                             // 角色唯一标识
    std::string name;                           // 角色姓名
    std::string role = "supporting";            // protagonist|antagonist|supporting|minor
    std::string age;                            // 年龄，例如 "28" 或 "二十多岁末"
    std::string appearance;                     // 外貌描述
    std::string personality;                    // 性格与行为习惯
    std::string background;                     // 背景经历
    std::string goal;                           // 当前最想达成的目标
    std::string motivation;                     // 目标背后的深层动机
    std::string internal_conflict;              // 角色的内在冲突
    std::string external_conflict;              // 角色面对的外在冲突
    std::string secret;                         // 暂不应轻易暴露的秘密
    std::string fear;                           // 角色的核心恐惧
    std::string misbelief;                      // 角色抱持的错误信念
    std::string speaking_style;                 // 角色的说话风格
    std::vector<std::string> traits;            // 性格特征，例如 ["brave", "impulsive"]
    std::vector<std::string> core_values;       // 核心价值观，例如 ["loyalty"]
    std::vector<std::string> taboos;            // 角色不会做或不能做的事
    std::vector<Relationship> relationships;    // 与其他角色的结构化关系列表
    std::vector<std::string> chapter_appearances; // 角色出场过的章节 ID
    std::string arc;                            // 角色弧光与成长轨迹摘要
    std::string notes;                          // 自由补充备注
    std::vector<std::string> tags;              // 轻量分类标签，如 "core-cast"
    GenerationControl generation;               // 控制角色字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 角色扩展信息
};

// Character 的 JSON 序列化。
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

// Character 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  Setting — 世界观中的地点、组织或物品
// ──────────────────────────────────────────────

struct Setting {
    std::string id;                             // 设定唯一标识
    std::string name;                           // 设定名称，例如 "Thorne University"
    std::string category = "location";          // location|organization|item|rule|other
    std::string description;                    // 设定描述
    std::string story_function;                 // 叙事功能，例如 "safe-haven"、"mystery-gateway"
    std::string sensory_profile;                // 感官印象，例如 "dust, stone, whispers, cold iron"
    std::vector<std::string> related_characters;    // 关联角色 ID
    std::vector<std::string> related_plot_threads;  // 关联剧情线 ID
    std::vector<std::string> related_rule_ids;      // 关联世界规则 ID
    std::string notes;                          // 自由补充备注
    std::vector<std::string> tags;              // 轻量分类标签，如 "campus"、"ancient"
    GenerationControl generation;               // 控制设定字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 设定扩展信息
};

// Setting 的 JSON 序列化。
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

// Setting 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  PlotThread — 剧情线
// ──────────────────────────────────────────────

struct PlotThread {
    std::string id;                             // 剧情线唯一标识
    std::string name;                           // 剧情线名称，例如 "主线任务"、"感情线"
    std::string description;                    // 剧情线概要描述
    std::string type = "main";                  // main|subplot|romance|mystery|political|other
    std::string status = "planned";             // planned|active|paused|resolved
    int priority = 0;                           // 优先级，越高越优先被当前生成关注
    std::string stakes;                         // 失败的代价
    std::string central_question;               // 这条剧情线最终要回答的核心问题
    std::string resolution;                     // 预期收束方式
    std::string start_chapter_id;               // 起始章节 ID
    std::string end_chapter_id;                 // 结束章节 ID
    std::vector<std::string> related_characters;    // 关联角色 ID
    std::vector<std::string> related_settings;      // 关联设定 ID
    std::vector<std::string> tags;              // 轻量分类标签
    GenerationControl generation;               // 控制剧情线字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 剧情线扩展信息
};

// PlotThread 的 JSON 序列化。
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

// PlotThread 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  Outline — 大纲
// ──────────────────────────────────────────────

struct Outline {
    std::string premise;                        // 一句话故事前提
    std::string story_structure;                // 故事结构，例如 "three-act"、"hero-journey"
    std::vector<std::string> act_summaries;     // 各幕（三段/四段）的情节摘要
    std::vector<PlotThread> plot_threads;       // 剧情线列表
    std::vector<Chapter> chapters;              // 章节列表
    std::vector<std::string> tags;              // 轻量分类标签
    GenerationControl generation;               // 控制大纲字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 大纲扩展信息
};

// Outline 的 JSON 序列化。
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

// Outline 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  Style — 写作风格配置
// ──────────────────────────────────────────────

struct Style {
    std::string tone = "neutral";               // atmospheric|dark|light|neutral|...
    std::string pacing = "moderate";            // slow|moderate|fast
    std::string pov = "third_person_limited";   // first_person|third_person_limited|third_person_omniscient
    std::string tense = "past";                 // past|present
    std::string prose_style = "literary";       // literary|commercial|minimalist|descriptive
    std::string dialogue_style = "naturalistic"; // naturalistic|stylized|minimal
    std::string narrative_distance = "close";    // close|medium|distant
    int chapter_length_target = 4000;           // 目标章节字数
    std::string sentence_length = "varied";     // short|medium|long|varied
    std::string vocabulary = "rich";            // simple|moderate|rich
    std::string voice_reference;                // 叙事声音参考，例如 "类似村上春树"
    std::string show_vs_tell_bias = "balanced"; // show|balanced|tell
    std::string dialogue_density = "moderate";  // sparse|moderate|dense
    std::string description_density = "moderate"; // sparse|moderate|dense
    std::string introspection_density = "moderate"; // sparse|moderate|dense
    std::string humor_level = "low";            // none|low|moderate|high
    std::string sensory_focus;                  // 感官描写重点，例如 "visual, tactile"
    std::vector<std::string> forbidden_phrases; // 禁止使用的词语列表，例如 "suddenly"
    std::vector<std::string> forbidden_tropes;  // 禁止使用的套路列表
    std::string chapter_opening_style;          // 章节开头风格
    std::string chapter_ending_style;           // 章节结尾风格
    std::string notes;                          // 自由补充备注
    std::vector<std::string> tags;              // 轻量分类标签，如 "moody"
    GenerationControl generation;               // 控制风格字段的提示词参与度
    std::map<std::string, nlohmann::json> metadata; // 风格扩展信息
};

// Style 的 JSON 序列化。
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

// Style 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
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

// ──────────────────────────────────────────────
//  Project — 小说项目顶层元数据
// ──────────────────────────────────────────────

struct Project {
    // ── 元数据 ──
    int format_version = 3;                     // 数据格式版本号，便于后续兼容升级
    std::string title;                          // 小说标题
    std::string author;                         // 作者名
    std::string description;                    // 书籍简介
    std::string logline;                        // 一句话卖点（pitch）
    std::string theme;                          // 全书主题
    std::string central_question;               // 全书要回答的核心问题
    std::string target_audience;                // 目标读者群，例如 "Adult fantasy readers"
    std::vector<std::string> genre;             // 类型标签，例如 ["fantasy", "mystery"]
    std::vector<std::string> comps;             // 参考作品列表

    // ── 内容约束 ──
    std::string content_rating;                 // 内容评级，例如 "PG-13"
    std::vector<std::string> must_have_elements;    // 必须包含的元素
    std::vector<std::string> must_avoid_elements;   // 必须避免的元素
    std::vector<std::string> narrative_promises;    // 对读者的体验承诺
    std::string world_rules_summary;            // 世界规则总述
    std::string ending_type;                    // tragic|bittersweet|happy|open|twist

    // ── 进度 ──
    int target_word_count = 0;                  // 目标总字数
    int current_word_count = 0;                 // 当前已写字数
    std::string status = "planning";            // planning|in_progress|completed|on_hold

    // ── 时间戳 ──
    std::string created;                        // 创建时间（ISO 8601 UTC）
    std::string modified;                       // 最后修改时间（ISO 8601 UTC）

    // ── 扩展与控制 ──
    std::vector<std::string> tags;              // 项目级标签，如题材、优先级
    GenerationControl generation;               // 项目级提示词控制
    std::map<std::string, nlohmann::json> metadata; // 项目级扩展信息

    // ── 运行期字段（不参与序列化） ──
    std::string path;                           // 项目目录路径，由文件系统位置推导

    // ── 子对象（分别独立 JSON 文件存储） ──
    Outline outline;                            // 大纲
    std::vector<Character> characters;          // 角色列表
    std::vector<Setting> settings;              // 设定列表
    std::vector<WorldRule> world_rules;         // 世界规则列表
    Style style;                                // 写作风格配置
};

// Project 的 JSON 序列化（path 和子对象不写入 project.json）。
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

// Project 的 JSON 反序列化，缺失字段回落默认值，未知字段进入 metadata。
inline void from_json(const nlohmann::json& j, Project& p) {
    using namespace project::model_detail;
    // Project 只保留稳定的顶层元数据；其余扩展字段统一落入 metadata。
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
