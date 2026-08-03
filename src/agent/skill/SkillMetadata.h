#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

// 技能声明的斜杠命令（如 /commit，frontmatter commands 列表项）。
struct SkillCommand {
    std::string name;         // 命令名（不含斜杠，如 "commit"）
    std::string description;  // 命令说明
};

// 单个技能的元数据与按需加载的正文，对应一个 SKILL.md，
// 贯穿发现、注册、加载全流程。
struct SkillMetadata {
    std::string name;             // 技能名（frontmatter 缺省时取目录名）
    std::string description;      // 一句话描述（列入 <available_skills> 目录，供 LLM 判断是否匹配）
    // always=true: 全文常驻 system prompt；
    // 否则仅在目录中列出名称/描述，由 LLM 通过 use_skill 工具按需加载。
    bool always = false;
    // 用户级开关：禁用的技能对 LLM 完全隐藏（不进目录、use_skill 拒绝加载）。
    bool enabled = true;
    std::filesystem::path root_dir;           // 技能目录（SKILL.md 所在目录）
    std::vector<SkillCommand> commands;       // 技能声明的斜杠命令列表

    // 正文按需加载缓存（mutable：SkillRegistry 的 const 查询接口也能懒加载）
    mutable bool content_loaded = false;      // 区分“未加载/读取失败”(false) 与“已加载”(true)
    mutable std::string content;              // SKILL.md 正文（frontmatter 之后部分）
};

} // namespace skill
