#include "skill/SkillRegistry.h"

#include <sstream>
#include <unordered_set>

namespace skill {

SkillRegistry::SkillRegistry(SkillLoader loader)
    : loader_(std::move(loader)) {}

void SkillRegistry::addSearchPath(std::filesystem::path dir) {
    search_paths_.push_back(std::move(dir));
}

void SkillRegistry::discoverAll() {
    skills_.clear();
    std::unordered_set<std::string> seen;

    for (const auto& dir : search_paths_) {
        for (auto& skill : loader_.discover(dir)) {
            if (seen.count(skill.name))
                continue;
            seen.insert(skill.name);
            skills_.push_back(std::move(skill));
        }
    }
}

const SkillMetadata* SkillRegistry::get(const std::string& name) const {
    for (auto& s : skills_) {
        if (s.name == name) {
            loader_.ensureLoaded(s);
            return &s;
        }
    }
    return nullptr;
}

std::vector<SkillMetadata> SkillRegistry::listSkills() const {
    return skills_;
}

std::string SkillRegistry::getSkillContext() const {
    std::ostringstream ctx;
    for (auto& s : skills_) {
        loader_.ensureLoaded(s);

        if (!s.emoji.empty())
            ctx << s.emoji << " ";
        ctx << "### " << s.name << "\n";
        if (!s.description.empty())
            ctx << s.description << "\n\n";
        ctx << s.content << "\n";

        if (!s.commands.empty()) {
            ctx << "**Commands:**\n";
            for (const auto& cmd : s.commands) {
                ctx << "- `/" << cmd.name << "`";
                if (!cmd.description.empty())
                    ctx << " — " << cmd.description;
                ctx << "\n";
            }
        }
        ctx << "\n";
    }
    return ctx.str();
}

bool SkillRegistry::hasSkill(const std::string& name) const {
    for (const auto& s : skills_) {
        if (s.name == name)
            return true;
    }
    return false;
}

std::vector<SkillCommand> SkillRegistry::getAllCommands() const {
    std::vector<SkillCommand> cmds;
    for (const auto& s : skills_) {
        for (const auto& c : s.commands)
            cmds.push_back(c);
    }
    return cmds;
}

} // namespace skill
