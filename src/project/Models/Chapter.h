#pragma once

// Chapter — 章节（写给 AI 的"章级创作简报"）。
//
// 包含章节的全部元信息与创作指引，供 AI 根据此结构生成正文内容。
// 同时也支持 JSON 序列化（to_json / from_json）。

#include "project/Models/ModelDetail.h"
#include "project/Models/Scene.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Chapter {
    // ---- 标识 ----
    std::string id;                    //< 章节唯一标识符（UUID 或自定义）
    std::string title;                 //< 章节标题
    int order = 0;                     //< 章节序号（在卷内的顺序）

    // ---- 叙事核心要素 ----
    std::string synopsis;              //< 章节概要：本章发生的核心剧情简述
    std::string goal;                  //< 本章目标：主角/叙事在本章要达成的目的
    std::string conflict;              //< 核心冲突：阻碍目标实现的主要矛盾
    std::string outcome;               //< 本章结局：剧情走向的结果或 cliffhanger

    // ---- 节奏与技巧 ----
    std::string turning_point;         //< 转折点：本章中剧情发生转折的事件
    std::string hook;                  //< 钩子：开篇吸引读者继续阅读的悬念/爆点
    std::string reveal;                //< 揭示：本章揭露的秘密或新信息
    std::string foreshadowing;         //< 伏笔：为后续章节埋下的线索
    std::string payoff;                //< 回报：对前文伏笔的呼应/回收
    std::string emotional_beat;        //< 情感节奏：本章应有的情感基调/弧线描写

    // ---- 时空定位 ----
    std::string location_id;           //< 主要场景地点 ID（关联 Settings）
    std::string time_marker;           //< 时间标记（如 "三天后"、"夜晚"）
    std::string volume_id;             //< 所属卷 ID（关联 Volume）

    // ---- 子结构 ----
    std::vector<Scene> scenes;         //< 场景列表：本章拆解为多个场景的详细编排

    // ---- 角色与事件 ----
    std::vector<std::string> pov_characters;       //< POV 角色列表（视角跟随谁）
    std::vector<std::string> key_events;           //< 关键事件列表（本章必须发生的事件）
    std::vector<std::string> themes;               //< 本章涉及的主题标签
    std::vector<std::string> active_plot_threads;  //< 活跃故事线 ID 列表
    std::vector<std::string> focus_characters;     //< 重点刻画角色列表
    std::vector<std::string> focus_settings;       //< 重点描写的场景/环境列表

    // ---- 项目管理 ----
    std::string status = "outlined";   //< 写作状态：outlined / drafting / revised / final
    std::string file_path;             //< 章节正文的存储文件路径
    int word_count = 0;                //< 字数统计

    // ---- 扩展 ----
    std::map<std::string, nlohmann::json> metadata;    //< 扩展元数据（兼容未来字段）
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
        "focus_characters", "focus_settings", "volume_id", "status", "word_count",
        "file_path", "metadata"
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
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
