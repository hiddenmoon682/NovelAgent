#pragma once

#include "agent/skill/SkillMetadata.h"

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

class SkillLoader {
public:
    std::vector<SkillMetadata> discover(const std::filesystem::path& dir) const;
    void ensureLoaded(SkillMetadata& skill) const;
    bool checkGating(const SkillMetadata& skill) const;

private:
    SkillMetadata parseFrontmatter(const std::filesystem::path& file) const;
    bool isBinaryAvailable(const std::string& bin) const;
    bool isEnvAvailable(const std::string& env) const;
    std::string currentOS() const;
};

} // namespace skill
