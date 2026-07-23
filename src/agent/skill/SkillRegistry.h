#pragma once

#include "agent/skill/ISkillProvider.h"
#include "agent/skill/SkillLoader.h"

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

class SkillRegistry : public ISkillProvider {
public:
    explicit SkillRegistry(SkillLoader loader = {});

    void addSearchPath(std::filesystem::path dir);
    void discoverAll();

    const SkillMetadata* get(const std::string& name) const;

    std::vector<SkillMetadata> listSkills() const override;
    std::string getSkillContext() const override;
    bool hasSkill(const std::string& name) const override;
    std::vector<SkillCommand> getAllCommands() const override;

private:
    SkillLoader loader_;
    std::vector<std::filesystem::path> search_paths_;
    mutable std::vector<SkillMetadata> skills_;
};

} // namespace skill
