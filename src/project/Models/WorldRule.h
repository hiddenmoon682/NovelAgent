#pragma once

// WorldRule — 世界规则（适合奇幻、科幻、悬疑等需要"规则一致性"的小说）。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct WorldRule {
    std::string id, name, summary, limitations, costs, exceptions, known_by;
    std::vector<std::string> related_settings;
    // A16: 规则冲突与优先级——支撑自动化规则一致性检测。
    //   precedence: 数字越大越优先；两条规则冲突时高优先级胜出（默认 0 = 无优先级）。
    //   contradicts_with: 显式声明与本规则互斥的世界规则 ID 列表，
    //     供一致性检查工具发现"同章引用了互相矛盾规则"等问题。
    int precedence = 0;
    std::vector<std::string> contradicts_with;
    std::map<std::string, nlohmann::json> metadata;

    // 生成用于向量检索嵌入的世界规则描述文本。
    //
    // 字段清单即嵌入内容（precedence/contradicts_with/related_settings 等
    // 一致性检测用结构字段不进入嵌入）：新增字段若需进入检索，在此补充即可；
    // 调用方（NovelChunker::chunkWorldRule）无需同步修改。
    std::string toEmbeddingText() const
    {
        std::ostringstream ss;

        ss << "世界规则: " << name << "\n";

        if (!summary.empty()) {
            ss << "概要: " << summary << "\n";
        }
        if (!limitations.empty()) {
            ss << "限制: " << limitations << "\n";
        }
        if (!costs.empty()) {
            ss << "代价: " << costs << "\n";
        }
        if (!exceptions.empty()) {
            ss << "例外: " << exceptions << "\n";
        }
        if (!known_by.empty()) {
            ss << "知晓范围: " << known_by << "\n";
        }

        return ss.str();
    }
};

inline void to_json(nlohmann::json& j, const WorldRule& r) {
    j = nlohmann::json{
        {"id", r.id}, {"name", r.name}, {"summary", r.summary},
        {"limitations", r.limitations}, {"costs", r.costs},
        {"exceptions", r.exceptions}, {"known_by", r.known_by},
        {"related_settings", r.related_settings},
        {"precedence", r.precedence},
        {"contradicts_with", r.contradicts_with},
        {"metadata", r.metadata}
    };
}

inline void from_json(const nlohmann::json& j, WorldRule& r) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "id", "name", "summary", "limitations", "costs", "exceptions",
        "known_by", "related_settings", "precedence", "contradicts_with", "metadata"
    };
    r.id = utils::json::getOrDefault(j, "id", std::string{});
    r.name = utils::json::getOrDefault(j, "name", std::string{});
    r.summary = utils::json::getOrDefault(j, "summary", std::string{});
    r.limitations = utils::json::getOrDefault(j, "limitations", std::string{});
    r.costs = utils::json::getOrDefault(j, "costs", std::string{});
    r.exceptions = utils::json::getOrDefault(j, "exceptions", std::string{});
    r.known_by = utils::json::getOrDefault(j, "known_by", std::string{});
    r.related_settings = utils::json::getOrDefault(j, "related_settings", std::vector<std::string>{});
    r.precedence = utils::json::getOrDefault(j, "precedence", 0);
    r.contradicts_with = utils::json::getOrDefault(j, "contradicts_with", std::vector<std::string>{});
    r.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
