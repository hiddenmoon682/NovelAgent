#pragma once

// Setting — 世界观中的地点、组织或物品。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct Setting {
    std::string id, name, category = "location", description, story_function, sensory_profile, notes;
    std::vector<std::string> related_characters, related_plot_threads, related_rule_ids;
    std::map<std::string, nlohmann::json> metadata;

    // 生成用于向量检索嵌入的设定描述文本。
    //
    // 字段清单即嵌入内容（notes/关联列表等创作元数据不进入嵌入）：
    // 新增字段若需进入检索，在此补充即可；调用方（NovelChunker::chunkSetting）无需同步修改。
    std::string toEmbeddingText() const
    {
        std::ostringstream ss;

        ss << "设定: " << name;
        if (!category.empty()) {
            ss << " [" << category << "]";
        }
        ss << "\n";

        if (!description.empty()) {
            ss << "描述: " << description << "\n";
        }
        if (!story_function.empty()) {
            ss << "叙事功能: " << story_function << "\n";
        }
        if (!sensory_profile.empty()) {
            ss << "感官印象: " << sensory_profile << "\n";
        }

        return ss.str();
    }
};

inline void to_json(nlohmann::json& j, const Setting& s) {
    j = nlohmann::json{
        {"id", s.id}, {"name", s.name}, {"category", s.category},
        {"description", s.description}, {"story_function", s.story_function},
        {"sensory_profile", s.sensory_profile},
        {"related_characters", s.related_characters},
        {"related_plot_threads", s.related_plot_threads},
        {"related_rule_ids", s.related_rule_ids}, {"notes", s.notes},
        {"metadata", s.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Setting& s) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "category", "description", "story_function",
        "sensory_profile", "related_characters", "related_plot_threads",
        "related_rule_ids", "notes", "metadata"
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
    s.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
