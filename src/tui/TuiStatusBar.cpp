/// TuiStatusBar 实现 — Claude Code 风格单行状态栏。

#include "tui/TuiStatusBar.h"

#include <ftxui/dom/elements.hpp>

using namespace ftxui;

TuiStatusBar::TuiStatusBar() = default;

void TuiStatusBar::setMode(const std::string& mode) {
    mode_ = mode;
}

void TuiStatusBar::updateTokens(int totalTokens) {
    token_usage_ += totalTokens;
}

void TuiStatusBar::setProjectInfo(const std::string& name, int chapterCount, int charCount) {
    project_name_ = name;
    chapter_count_ = chapterCount;
    char_count_ = charCount;
}

Element TuiStatusBar::render() const {
    Elements parts;

    // 项目名 + 章节数
    if (!project_name_.empty()) {
        parts.push_back(text(" " + project_name_) | bold | color(Color::Cyan));
        parts.push_back(text(" " + std::to_string(chapter_count_) + "章 "
                             + std::to_string(char_count_) + "角色") | dim);
        parts.push_back(separator());
    }

    // 模式指示
    Color modeColor = Color::Green;
    if (mode_ == "思考中" || mode_ == "执行工具") modeColor = Color::Yellow;
    else if (mode_ == "错误") modeColor = Color::Red;

    parts.push_back(text(" " + mode_ + " ") | color(modeColor));

    // Token 用量
    if (token_usage_ > 0) {
        parts.push_back(separator());
        parts.push_back(text(" " + std::to_string(token_usage_) + " tokens") | dim);
    }

    // 填充剩余空间
    parts.push_back(filler());

    // 退出提示
    parts.push_back(text("Esc 退出 ") | dim);

    return hbox(std::move(parts)) | size(HEIGHT, EQUAL, 1);
}
