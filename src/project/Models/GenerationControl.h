#pragma once

/// GenerationControl — 提示词组装控制器。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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

inline bool shouldUseField(
    const GenerationControl& generation,
    const std::string& fieldName,
    const std::vector<std::string>& objectTags = {}) {
    using namespace project::model_detail;
    if (!generation.enabled) return false;
    if (!generation.required_tags.empty() && !hasAnyTag(generation.required_tags, objectTags))
        return false;
    if (!generation.blocked_tags.empty() && hasAnyTag(generation.blocked_tags, objectTags))
        return false;
    if (!generation.include_fields.empty() && !contains(generation.include_fields, fieldName))
        return false;
    if (contains(generation.exclude_fields, fieldName)) return false;
    return true;
}
