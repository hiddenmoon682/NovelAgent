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
    bool always = false;
    std::vector<std::string> required_bins;
    std::vector<std::string> required_envs;
    std::vector<std::string> os_restrict;
    std::filesystem::path root_dir;
    std::vector<SkillCommand> commands;

    mutable bool content_loaded = false;
    mutable std::string content;
};

} // namespace skill
