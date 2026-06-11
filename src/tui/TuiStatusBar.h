#pragma once

/// FTXUI TUI 状态栏 — 顶部信息栏。
///
/// 显示：
/// - 当前模式（就绪/思考中/执行工具/错误）
/// - Token 用量统计
/// - 项目名称 + 章节/角色数

#include <ftxui/dom/elements.hpp>
#include <string>

/// 状态栏组件，实时显示 Agent 运行状态。
class TuiStatusBar {
public:
    TuiStatusBar();

    /// 设置当前模式标签。
    void setMode(const std::string& mode);
    /// 更新 Token 用量。
    void updateTokens(int totalTokens);
    /// 设置项目信息。
    void setProjectInfo(const std::string& name, int chapterCount, int charCount);

    /// 渲染状态栏为 FTXUI Element（单行）。
    ftxui::Element render() const;

private:
    std::string mode_ = "就绪";
    int token_usage_ = 0;
    std::string project_name_;
    int chapter_count_ = 0;
    int char_count_ = 0;
};
