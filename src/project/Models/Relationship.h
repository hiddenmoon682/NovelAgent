#pragma once

// Relationship — 角色之间的关系。

#include "project/Models/ModelDetail.h"

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct Relationship {
    // 关系主体
    std::string target_character_id;             // 关系指向的目标角色 ID（目标角色删除时本条关系被级联移除）
    std::string type;                            // 关系类型（如师徒、仇敌、恋人）
    std::string description;                     // 关系描述（相处方式与过往历史）

    // 公开面与私下面
    std::string public_status;                   // 外界眼中的关系状态（公开面）
    std::string private_feeling;                 // 角色私下的真实感受，与公开面形成反差（戏剧张力来源）

    // 状态与张力
    std::string status = "active";               // 关系存续状态（默认 active）
    int tension = 0;                             // 关系张力（数值越大冲突越强，默认 0）

    std::map<std::string, nlohmann::json> metadata; // 扩展元数据：未知 JSON 字段兜底收纳，保持前后向兼容
};

inline void to_json(nlohmann::json& j, const Relationship& r) {
    j = nlohmann::json{
        {"target_character_id", r.target_character_id}, {"type", r.type},
        {"description", r.description}, {"public_status", r.public_status},
        {"private_feeling", r.private_feeling}, {"status", r.status},
        {"tension", r.tension}, {"metadata", r.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Relationship& r) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "target_character_id", "type", "description", "public_status",
        "private_feeling", "status", "tension", "metadata"
    };
    r.target_character_id = utils::json::getOrDefault(j, "target_character_id", std::string{});
    r.type = utils::json::getOrDefault(j, "type", std::string{});
    r.description = utils::json::getOrDefault(j, "description", std::string{});
    r.public_status = utils::json::getOrDefault(j, "public_status", std::string{});
    r.private_feeling = utils::json::getOrDefault(j, "private_feeling", std::string{});
    r.status = utils::json::getOrDefault(j, "status", std::string{"active"});
    r.tension = utils::json::getOrDefault(j, "tension", 0);
    r.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
