#pragma once

// Character — 角色完整档案。

#include "project/Models/Relationship.h"
#include "project/Models/CharacterDevelopment.h"

#include <nlohmann/json.hpp>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct Character {
    // 基础身份信息
    std::string id;                              // 角色唯一标识（如 char-001），项目内引用与向量索引均以此为准
    std::string name;                            // 角色名
    std::string role = "supporting";             // 角色定位（默认 supporting 配角，如 protagonist 主角）
    std::string age;                             // 年龄（文本描述，允许模糊表达如"二十出头"）
    std::string appearance;                      // 外貌描写
    std::string personality;                     // 性格特征
    std::string background;                      // 身世背景

    // 叙事驱动要素
    std::string goal;                            // 角色当前故事线中追求的目标
    std::string motivation;                      // 驱动目标的内在动机（为何要达成目标）
    std::string internal_conflict;               // 内在冲突：欲望与信念的自我拉扯
    std::string external_conflict;               // 外在冲突：与其他人、势力或环境的对抗
    std::string secret;                          // 隐瞒的秘密（可作伏笔或爆点）
    std::string fear;                            // 恐惧（制造弱点的来源）
    std::string misbelief;                       // 错误信念（阻碍角色成长的执念）
    std::string speaking_style;                  // 说话风格（口癖、用词与语气习惯）

    // 特质与价值观
    std::vector<std::string> traits;             // 特征标签列表（如"左腕疤""黑布蒙面"）
    std::vector<std::string> core_values;        // 核心价值观列表（不可动摇的底线）
    std::vector<std::string> taboos;             // 禁忌列表（角色绝不触碰的红线）

    // 关系与登场
    std::vector<Relationship> relationships;     // 与其他角色的关系网（见 Relationship）
    std::vector<std::string> chapter_appearances; // 登场章节 ID 列表（章节删除时级联清理）

    // 成长轨迹
    std::string arc;                             // 角色弧光：整体成长变化的概述
    std::string notes;                           // 备注：创作笔记与设定补充
    std::vector<CharacterDevelopment> development; // 分章节发展记录（见 CharacterDevelopment）

    std::map<std::string, nlohmann::json> metadata; // 扩展元数据：未知 JSON 字段兜底收纳，保持前后向兼容

    // 生成用于向量检索嵌入的角色描述文本。
    //
    // 字段清单即嵌入内容：新增字段若需进入检索，在此补充即可；
    // 调用方（NovelChunker::chunkCharacter）不感知字段清单，无需同步修改。
    std::string toEmbeddingText() const
    {
        std::ostringstream ss;

        ss << "角色: " << name;
        if (!role.empty()) {
            ss << " (" << role << ")";
        }
        ss << "\n";

        if (!goal.empty()) {
            ss << "目标: " << goal << "\n";
        }
        if (!motivation.empty()) {
            ss << "动机: " << motivation << "\n";
        }
        if (!personality.empty()) {
            ss << "性格: " << personality << "\n";
        }
        if (!internal_conflict.empty()) {
            ss << "内在冲突: " << internal_conflict << "\n";
        }
        if (!external_conflict.empty()) {
            ss << "外在冲突: " << external_conflict << "\n";
        }
        if (!speaking_style.empty()) {
            ss << "说话风格: " << speaking_style << "\n";
        }
        if (!arc.empty()) {
            ss << "角色弧光: " << arc << "\n";
        }
        if (!traits.empty()) {
            ss << "特征: ";
            for (size_t i = 0; i < traits.size(); ++i) {
                if (i > 0) ss << "、";
                ss << traits[i];
            }
            ss << "\n";
        }
        if (!fear.empty()) {
            ss << "恐惧: " << fear << "\n";
        }
        if (!misbelief.empty()) {
            ss << "错误信念: " << misbelief << "\n";
        }

        return ss.str();
    }
};

inline void to_json(nlohmann::json& j, const Character& c) {
    j = nlohmann::json{
        {"id", c.id}, {"name", c.name}, {"role", c.role}, {"age", c.age},
        {"appearance", c.appearance}, {"personality", c.personality},
        {"background", c.background}, {"goal", c.goal}, {"motivation", c.motivation},
        {"internal_conflict", c.internal_conflict}, {"external_conflict", c.external_conflict},
        {"secret", c.secret}, {"fear", c.fear}, {"misbelief", c.misbelief},
        {"speaking_style", c.speaking_style}, {"traits", c.traits},
        {"core_values", c.core_values}, {"taboos", c.taboos},
        {"relationships", c.relationships}, {"chapter_appearances", c.chapter_appearances},
        {"arc", c.arc}, {"notes", c.notes}, {"development", c.development},
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
        "development", "metadata"
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
    c.development = utils::json::getOrDefault(j, "development", std::vector<CharacterDevelopment>{});
    c.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
