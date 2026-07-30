#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

// 技能携带的斜杠命令（如 /commit），由 frontmatter 的 commands 数组解析而来。
struct SkillCommand {
    std::string name;         // 命令名（不含斜杠，如 "commit"）
    std::string description;  // 命令说明
};

// 技能元数据 — 一个 SKILL.md 的完整描述，贯穿发现、门控、注册、加载全流程。
struct SkillMetadata {
    std::string name;         // 技能名（缺省时回退为目录名）
    std::string description;  // 技能描述（列入按需目录，供 LLM 判断是否匹配）
    std::string emoji;        // 展示用表情符号（常驻技能渲染在标题前）
    // always=true: 全文常驻 system prompt（并跳过环境门控）；
    // 否则仅在目录中列出名称/描述，由 LLM 通过 use_skill 工具按需加载。
    bool always = false;
    // 用户级开关：禁用的技能对 LLM 完全隐藏（不进目录、use_skill 拒绝加载）。
    bool enabled = true;
    std::vector<std::string> required_bins;  // 必需的可执行文件（PATH 中须存在）
    std::vector<std::string> required_envs;  // 必需的环境变量（须已设置且非空）
    std::vector<std::string> os_restrict;    // 允许的操作系统（空表示不限）
    std::filesystem::path root_dir;          // 技能根目录（SKILL.md 所在目录）
    std::vector<SkillCommand> commands;      // 技能提供的斜杠命令列表

    // 正文懒加载缓存（mutable：const 方法 ensureLoaded 也可写入）。
    // content_loaded 区分“未加载/读取失败”(false) 与“已加载”(true)。
    mutable bool content_loaded = false;
    mutable std::string content;             // SKILL.md frontmatter 之后的正文
};

} // namespace skill
