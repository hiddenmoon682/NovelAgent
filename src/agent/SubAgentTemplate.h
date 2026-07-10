#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace agent {

// 子 Agent 模板 — 预定义的子 Agent 配置。
struct SubAgentTemplate {
    std::string name;                    // 模板名
    std::string description;             // 用途说明
    std::string system_prompt;           // 子 Agent 的 system prompt
    std::vector<std::string> allowed_tools;  // 允许使用的工具
    std::string model;                   // 可选：指定模型（空=使用默认）
    int suggested_max_rounds = 3;        // A18: 建议的最大 tool_call 轮数（按模板差分配）
    bool built_in = false;               // 内置模板不可删除
};

// 内置模板工厂 — 返回 5 个预设模板。
inline std::vector<SubAgentTemplate> builtInTemplates() {
    return {
        {
            "chapter-consistency",
            "检查章节间逻辑/时间线/角色一致性",
            "你是小说一致性检查专家。仔细阅读指定章节，找出逻辑矛盾、时间线不一致、角色行为不一致等问题。每个问题要标注具体章节和行号。",
            {"read_chapter", "get_character", "get_outline"},
            "", 8, true
        },
        {
            "character-arc",
            "分析角色成长弧光完整性",
            "你是角色发展分析专家。追踪指定角色的成长轨迹，分析其动机变化、关键转折点、弧光是否完整。",
            {"get_character", "read_chapter"},
            "", 5, true
        },
        {
            "worldbuilding",
            "检查世界观设定一致性",
            "你是世界观设定审查专家。检查小说中的设定是否前后一致，规则是否有矛盾，例外是否合理。",
            {"get_setting", "get_world_rule", "read_chapter"},
            "", 8, true
        },
        {
            "grammar-style",
            "检查语法和文风统一性",
            "你是文风编辑。检查章节的语法错误、用词一致性、风格统一性。给出具体的修改建议。",
            {"read_chapter"},
            "", 3, true
        },
        {
            "plot-thread",
            "追踪剧情线展开和收束",
            "你是剧情结构分析师。追踪指定剧情线在相关章节中的展开情况，分析是否每个伏笔都有回收。",
            {"read_chapter", "get_outline"},
            "", 5, true
        }
    };
}

} // namespace agent
