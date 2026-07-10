#pragma once

// 终端 GUI — Claude Code CLI 风格界面。
//
// Phase 5 新增：提供类 Claude Code 的终端交互体验，包括：
// - 颜色主题（角色区分）
// - 状态栏（模式/进度/token用量）
// - Markdown 渲染（粗体/斜体/代码块）
// - 命令历史
// - 进度指示器
// - Token 用量实时显示

#include "cli/AnsiTerminal.h"
#include "cli/IOutputChannel.h"
#include "llm/ILLMClient.h"

#include <deque>
#include <string>
#include <vector>

// 终端 GUI 控制器 — 管理 REPL 界面的渲染。
class TerminalGUI {
public:
    explicit TerminalGUI(IOutputChannel& out);

    // ── 主题输出 ──

    // 输出用户输入（蓝色加粗 "> " 前缀）。
    void writeUserInput(const std::string& text);

    // 输出助手回复（绿色，自动渲染 Markdown）。
    void writeAssistant(const std::string& text);

    // 输出工具调用信息（灰色）。
    void writeToolCall(const std::string& tool_name);

    // 输出工具结果摘要（灰色，截断）。
    void writeToolResult(const std::string& summary);

    // 输出思考链（暗灰色，可选折叠）。
    void writeThinking(const std::string& text);

    // 输出错误（红色）。
    void writeError(const std::string& text);

    // 输出警告（黄色）。
    void writeWarning(const std::string& text);

    // ── 状态栏 ──

    // 渲染状态栏：[模式] 进度 | token 用量。
    // mode     当前模式（如 "Serial", "Parallel", "Plan"）
    // tokens   最近一次请求的 token 用量
    // extra    额外信息（如 "ch-003 写作中"）
    void renderStatusBar(const std::string& mode, int tokens, const std::string& extra = "");

    // ── 进度指示器 ──

    // 开始显示旋转动画（如 "Thinking..."）。
    void startSpinner(const std::string& label);

    // 更新旋转动画帧。
    void tickSpinner();

    // 停止旋转动画。
    void stopSpinner();

    // ── Markdown 渲染 ──

    // 将 Markdown 文本渲染为 ANSI 格式。
    std::string renderMarkdown(const std::string& text) const;

    // ── 命令历史 ──

    void addToHistory(const std::string& line);
    const std::deque<std::string>& history() const { return history_; }

    // ── 标题/分隔线 ──

    void writeTitle(const std::string& title);
    void writeSeparator();

private:
    IOutputChannel& out_;
    std::deque<std::string> history_;
    bool spinner_active_ = false;
    int spinner_frame_ = 0;

    // 渲染 Markdown 粗体 **text** → ANSI 粗体。
    static std::string renderBold(const std::string& text);

    // 渲染 Markdown 代码块 ```...``` → ANSI 灰色背景。
    static std::string renderCodeBlock(const std::string& text);

    // 渲染 Markdown 斜体 *text* → ANSI 斜体。
    static std::string renderItalic(const std::string& text);
};
