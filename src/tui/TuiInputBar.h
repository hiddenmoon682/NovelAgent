#pragma once

/// FTXUI TUI 输入栏 — 用户输入组件。
///
/// 封装 FTXUI Input 组件，支持：
/// - 命令历史（↑ 上一条 / ↓ 下一条）
/// - Tab 补全（待对接 CommandParser）
/// - 回调：回车时触发 onSubmit

#include <ftxui/component/component.hpp>
#include <functional>
#include <string>
#include <deque>

/// 输入栏组件，管理用户输入和历史记录。
class TuiInputBar {
public:
    using SubmitCallback = std::function<void(const std::string&)>;

    TuiInputBar();

    /// 设置回车提交回调。
    void onSubmit(SubmitCallback cb);

    /// 返回 FTXUI Component（用于加入组件树）。
    ftxui::Component render();

    /// 清空当前输入。
    void clear();

    /// 设置输入占位文本。
    void setPlaceholder(const std::string& text);

private:
    std::string input_content_;          ///< 当前输入文本
    std::deque<std::string> history_;    ///< 命令历史
    int history_index_ = -1;             ///< 当前历史浏览位置
    SubmitCallback on_submit_;
    std::string placeholder_ = "输入消息或 / 命令...";

    void addToHistory(const std::string& line);
};
