#pragma once

/// Project → IProjectReader/IProjectWriter 适配器。
/// Project 是已有数据模型（plain struct），通过此适配器实现接口。

#include "project/IProjectAccess.h"
#include "project/ProjectIO.h"
#include <stdexcept>

/// 将 Project 适配为 IProjectReader/IProjectWriter。
/// 轻量包装器，不拥有 Project，仅持有引用。
class ProjectAccess : public IProjectReader, public IProjectWriter {
    Project& project_;
public:
    explicit ProjectAccess(Project& p) : project_(p) {}

    // ── IProjectReader ──
    const std::vector<Chapter>& chapters() const override
        { return project_.outline.chapters; }
    const std::vector<Character>& characters() const override
        { return project_.characters; }
    const std::vector<Setting>& settings() const override
        { return project_.settings; }
    const std::vector<WorldRule>& worldRules() const override
        { return project_.world_rules; }
    const Outline& outline() const override
        { return project_.outline; }
    const Style& style() const override
        { return project_.style; }
    const std::string& projectPath() const override
        { return project_.path; }
    const std::string& projectTitle() const override
        { return project_.title; }
    const std::string& logline() const override
        { return project_.logline; }
    const std::string& theme() const override
        { return project_.theme; }
    std::string readChapterFile(const std::string& fp) const override
        { return ProjectIO::readChapter(project_.path, fp); }

    // ── IProjectWriter ──
    std::vector<Chapter>& mutableChapters() override
        { return project_.outline.chapters; }
    std::vector<Character>& mutableCharacters() override
        { return project_.characters; }
    std::vector<Setting>& mutableSettings() override
        { return project_.settings; }
    std::vector<WorldRule>& mutableWorldRules() override
        { return project_.world_rules; }
    Outline& mutableOutline() override
        { return project_.outline; }
    void writeChapterFile(const std::string& fp, const std::string& content) override
        { ProjectIO::writeChapter(project_.path, fp, content); }
    void saveProject() override
        { ProjectIO::save(project_); }

    // ── 便捷: 获取底层 Project& ──
    Project& project() { return project_; }
    const Project& project() const { return project_; }
};
