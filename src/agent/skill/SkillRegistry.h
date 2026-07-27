#pragma once

#include "agent/skill/ISkillProvider.h"
#include "agent/skill/SkillLoader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace skill {

class SkillRegistry : public ISkillProvider {
public:
    explicit SkillRegistry(SkillLoader loader = {});

    void addSearchPath(std::filesystem::path dir);
    void discoverAll();

    const SkillMetadata* get(const std::string& name) const;

    // 按需加载技能正文（供 use_skill 工具使用）。
    // 技能不存在或已被禁用时返回 nullopt。
    std::optional<std::string> loadContent(const std::string& name) const;

    // 启用/禁用技能（返回技能是否存在）。禁用集合在 discoverAll 后仍保持。
    bool setEnabled(const std::string& name, bool enabled);
    void setDisabledSkills(const std::vector<std::string>& names);
    std::vector<std::string> disabledSkills() const;

    std::vector<SkillMetadata> listSkills() const override;
    std::string getSkillContext() const override;
    bool hasSkill(const std::string& name) const override;
    std::vector<SkillCommand> getAllCommands() const override;

private:
    SkillLoader loader_;
    std::vector<std::filesystem::path> search_paths_;
    mutable std::vector<SkillMetadata> skills_;
    // 用户禁用的技能名（持久化由调用方负责，这里只管运行时状态）
    std::unordered_set<std::string> disabled_names_;
};

} // namespace skill
