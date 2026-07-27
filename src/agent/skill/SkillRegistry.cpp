#include "agent/skill/SkillRegistry.h"

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
            skill.enabled = disabled_names_.count(skill.name) == 0;
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

std::optional<std::string> SkillRegistry::loadContent(const std::string& name) const {
    for (auto& s : skills_) {
        if (s.name != name)
            continue;
        if (!s.enabled)
            return std::nullopt; // 禁用技能对 LLM 不可见
        loader_.ensureLoaded(s);
        return s.content;
    }
    return std::nullopt;
}

bool SkillRegistry::setEnabled(const std::string& name, bool enabled) {
    if (enabled)
        disabled_names_.erase(name);
    else
        disabled_names_.insert(name);

    for (auto& s : skills_) {
        if (s.name == name) {
            s.enabled = enabled;
            return true;
        }
    }
    return false;
}

void SkillRegistry::setDisabledSkills(const std::vector<std::string>& names) {
    disabled_names_.clear();
    disabled_names_.insert(names.begin(), names.end());
    for (auto& s : skills_)
        s.enabled = disabled_names_.count(s.name) == 0;
}

std::vector<std::string> SkillRegistry::disabledSkills() const {
    return {disabled_names_.begin(), disabled_names_.end()};
}

std::vector<SkillMetadata> SkillRegistry::listSkills() const {
    return skills_;
}

// 渐进式披露：always 技能全文常驻；其余启用技能仅列入目录，
// 由 LLM 调用 use_skill 工具按需加载全文，避免技能增多时上下文膨胀。
std::string SkillRegistry::getSkillContext() const {
    std::ostringstream catalog;  // 按需技能目录
    std::ostringstream resident; // 常驻技能全文

    for (auto& s : skills_) {
        if (!s.enabled)
            continue;

        if (s.always) {
            loader_.ensureLoaded(s);
            if (!s.emoji.empty())
                resident << s.emoji << " ";
            resident << "### " << s.name << "\n";
            if (!s.description.empty())
                resident << s.description << "\n\n";
            resident << s.content << "\n";

            if (!s.commands.empty()) {
                resident << "**Commands:**\n";
                for (const auto& cmd : s.commands) {
                    resident << "- `/" << cmd.name << "`";
                    if (!cmd.description.empty())
                        resident << " — " << cmd.description;
                    resident << "\n";
                }
            }
            resident << "\n";
        } else {
            catalog << "- " << s.name;
            if (!s.description.empty())
                catalog << ": " << s.description;
            catalog << "\n";
        }
    }

    std::string catalog_str = catalog.str();
    std::string resident_str = resident.str();
    if (catalog_str.empty() && resident_str.empty())
        return {};

    std::ostringstream ctx;
    if (!catalog_str.empty()) {
        ctx << "以下技能可按需使用：当任务与某技能描述匹配时，"
               "先调用 use_skill 工具加载其完整内容，再按内容指引执行。\n"
            << "<available_skills>\n" << catalog_str << "</available_skills>\n";
    }
    if (!resident_str.empty()) {
        if (!catalog_str.empty())
            ctx << "\n";
        ctx << resident_str;
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
        if (!s.enabled)
            continue;
        for (const auto& c : s.commands)
            cmds.push_back(c);
    }
    return cmds;
}

} // namespace skill
