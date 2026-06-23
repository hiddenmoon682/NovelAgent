#pragma once

/// Project — 小说项目顶层元数据。

#include "project/Models/GenerationControl.h"
#include "project/Models/Outline.h"
#include "project/Models/Character.h"
#include "project/Models/Setting.h"
#include "project/Models/WorldRule.h"
#include "project/Models/Style.h"

#include <map>
#include <string>
#include <vector>

struct Project {
    // ── 元数据 ──
    int format_version = 4;
    std::string title, author, description, logline, theme, central_question, target_audience;
    std::vector<std::string> genre, comps;

    // ── 内容约束 ──
    std::string content_rating, world_rules_summary, ending_type;
    std::vector<std::string> must_have_elements, must_avoid_elements, narrative_promises;

    // ── 进度 ──
    int target_word_count = 0, current_word_count = 0;
    std::string status = "planning";

    // ── 时间戳 ──
    std::string created, modified;

    // ── 扩展与控制 ──
    std::vector<std::string> tags;
    GenerationControl generation;
    std::map<std::string, nlohmann::json> metadata;

    // ── 运行期字段（不参与序列化）──
    std::string path;

    // ── 子对象（分别独立 JSON 文件存储）──
    Outline outline;
    std::vector<Character> characters;
    std::vector<Setting> settings;
    std::vector<WorldRule> world_rules;
    Style style;
};

inline void to_json(nlohmann::json& j, const Project& p) {
    j = nlohmann::json{
        {"format_version", p.format_version}, {"title", p.title},
        {"author", p.author}, {"description", p.description},
        {"logline", p.logline}, {"theme", p.theme},
        {"central_question", p.central_question},
        {"target_audience", p.target_audience},
        {"genre", p.genre}, {"comps", p.comps},
        {"content_rating", p.content_rating},
        {"must_have_elements", p.must_have_elements},
        {"must_avoid_elements", p.must_avoid_elements},
        {"narrative_promises", p.narrative_promises},
        {"world_rules_summary", p.world_rules_summary},
        {"ending_type", p.ending_type},
        {"target_word_count", p.target_word_count},
        {"current_word_count", p.current_word_count},
        {"status", p.status}, {"created", p.created}, {"modified", p.modified},
        {"tags", p.tags}, {"generation", p.generation}, {"metadata", p.metadata}
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
