/// TuiSidebar 实现 — Claude Code 风格侧边栏。

#include "tui/TuiSidebar.h"

#include "project/Models.h"

#include <ftxui/dom/elements.hpp>

using namespace ftxui;

TuiSidebar::TuiSidebar() = default;

void TuiSidebar::setProject(std::shared_ptr<Project> project) {
    project_ = std::move(project);
    loadData();
}

void TuiSidebar::refresh() {
    loadData();
}

void TuiSidebar::loadData() {
    chapters_.clear();
    characters_.clear();

    if (!project_) return;

    for (const auto& ch : project_->outline.chapters) {
        chapters_.push_back({ch.id, ch.title, "第" + std::to_string(ch.order) + "章"});
    }

    for (const auto& c : project_->characters) {
        characters_.push_back({c.id, c.name, c.role});
    }
}

Element TuiSidebar::render() const {
    Elements items;

    // 标题
    if (!project_ || project_->title.empty()) {
        items.push_back(text(" NovelAgent") | bold | color(Color::Cyan));
    } else {
        items.push_back(text(" " + project_->title) | bold | color(Color::Cyan));
    }
    items.push_back(separatorEmpty());

    // ── 大纲 ──
    items.push_back(text(" 大纲") | bold | color(Color::Grey50));
    if (chapters_.empty()) {
        items.push_back(text("  暂无章节") | dim);
    } else {
        for (const auto& ch : chapters_) {
            items.push_back(text("  " + ch.label + " " + ch.id) | color(Color::Grey70));
        }
    }

    items.push_back(separatorEmpty());

    // ── 角色 ──
    items.push_back(text(" 角色") | bold | color(Color::Grey50));
    if (characters_.empty()) {
        items.push_back(text("  暂无角色") | dim);
    } else {
        for (const auto& c : characters_) {
            std::string line = "  " + c.label;
            if (!c.subtitle.empty()) line += " · " + c.subtitle;
            items.push_back(text(line) | color(Color::Grey70));
        }
    }

    return vbox(std::move(items)) | size(WIDTH, GREATER_THAN, 22);
}
