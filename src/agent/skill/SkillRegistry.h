#pragma once

#include "agent/skill/ISkillProvider.h"
#include "agent/skill/SkillLoader.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace skill {

// 技能注册表 — 技能的发现、查询、按需加载与启停管理。
// 实现 ISkillProvider 接口，供 system prompt 组装与 LLM 工具使用。
// 线程安全：内部以 shared_mutex 保护，支持 worker 与 UI 线程并发访问。
class SkillRegistry : public ISkillProvider {
public:
    // 构造：注入技能加载器（负责从磁盘读取技能元数据与正文）。
    explicit SkillRegistry(SkillLoader loader = {});

    // 追加一个技能搜索目录（项目级/全局），供 discoverAll 扫描。
    void addSearchPath(std::filesystem::path dir);
    // 扫描全部搜索路径，重建技能列表；按路径顺序去重，禁用状态在此重新套用。
    void discoverAll();

    // 按名查询技能元数据；找不到返回 nullptr。
    const SkillMetadata* get(const std::string& name) const;

    // 按需加载技能正文（供 use_skill 工具使用）。
    // 技能不存在、已被禁用或正文读取失败时返回 nullopt。
    std::optional<std::string> loadContent(const std::string& name) const;

    // 启用/禁用技能（返回技能是否存在）。禁用集合在 discoverAll 后仍保持。
    bool setEnabled(const std::string& name, bool enabled);
    // 批量设置禁用名单（启动时从持久化恢复），并同步刷新各技能 enabled 状态。
    void setDisabledSkills(const std::vector<std::string>& names);
    // 返回当前被禁用的技能名列表（供调用方持久化）。
    std::vector<std::string> disabledSkills() const;

    // ── ISkillProvider 接口实现 ──
    std::vector<SkillMetadata> listSkills() const override;     // 列出全部技能（值拷贝）
    std::string getSkillContext() const override;               // 组装注入 prompt 的技能上下文
    bool hasSkill(const std::string& name) const override;      // 判断技能是否存在
    std::vector<SkillCommand> getAllCommands() const override;  // 汇总所有启用技能的斜杠命令

private:
    SkillLoader loader_;                               // 技能加载器（磁盘读取 + 正文缓存）
    std::vector<std::filesystem::path> search_paths_;  // 技能搜索目录列表
    mutable std::vector<SkillMetadata> skills_;        // 已发现的技能元数据（mutable 供 const 方法缓存正文）
    // 用户禁用的技能名（持久化由调用方负责，这里只管运行时状态）
    std::unordered_set<std::string> disabled_names_;
    // 保护 skills_/disabled_names_：worker 线程（save_skill→discoverAll、
    // use_skill→loadContent）与 UI 线程（skillList/setEnabled）并发访问
    mutable std::shared_mutex mutex_;
};

} // namespace skill
