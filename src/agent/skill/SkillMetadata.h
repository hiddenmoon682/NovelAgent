#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

struct SkillCommand {
    std::string name;
    std::string description;
};

struct SkillMetadata {
    std::string name;
    std::string description;
    std::string emoji;
    // always=true: 全文常驻 system prompt（并跳过环境门控）；
    // 否则仅在目录中列出名称/描述，由 LLM 通过 use_skill 工具按需加载。
    bool always = false;
    // 用户级开关：禁用的技能对 LLM 完全隐藏（不进目录、use_skill 拒绝加载）。
    bool enabled = true;
    std::vector<std::string> required_bins;
    std::vector<std::string> required_envs;
    std::vector<std::string> os_restrict;
    std::filesystem::path root_dir;
    std::vector<SkillCommand> commands;

    mutable bool content_loaded = false;
    mutable std::string content;
};

} // namespace skill
