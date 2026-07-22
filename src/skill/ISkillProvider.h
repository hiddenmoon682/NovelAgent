#pragma once

#include "skill/SkillMetadata.h"

#include <string>
#include <vector>

namespace skill {

class ISkillProvider {
public:
    virtual ~ISkillProvider() = default;

    virtual std::vector<SkillMetadata> listSkills() const = 0;
    virtual std::string getSkillContext() const = 0;
    virtual bool hasSkill(const std::string& name) const = 0;
    virtual std::vector<SkillCommand> getAllCommands() const = 0;
};

} // namespace skill
