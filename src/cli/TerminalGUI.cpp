/// TerminalGUI 实现。

#include "cli/TerminalGUI.h"

#include <regex>

TerminalGUI::TerminalGUI(IOutputChannel& out) : out_(out) {}

// ============================================================================
// 主题输出
// ============================================================================

void TerminalGUI::writeUserInput(const std::string& text) {
    out_.write(Ansi::userInput() + "> " + text + Ansi::reset() + "\n");
}

void TerminalGUI::writeAssistant(const std::string& text) {
    out_.write(Ansi::assistant() + renderMarkdown(text) + Ansi::reset() + "\n");
}

void TerminalGUI::writeToolCall(const std::string& tool_name) {
    out_.write("  " + Ansi::toolCall() + Ansi::label(tool_name, Ansi::toolCall())
               + " " + Ansi::reset() + "\n");
}

void TerminalGUI::writeToolResult(const std::string& summary) {
    out_.write("  " + Ansi::toolCall() + Ansi::dim()
               + (summary.size() > 120 ? summary.substr(0, 120) + "..." : summary)
               + Ansi::reset() + "\n");
}

void TerminalGUI::writeThinking(const std::string& text) {
    out_.write(Ansi::thinking() + "[思考] " + text + Ansi::reset() + "\n");
}

void TerminalGUI::writeError(const std::string& text) {
    out_.write(Ansi::error() + "错误: " + text + Ansi::reset() + "\n");
}

void TerminalGUI::writeWarning(const std::string& text) {
    out_.write(Ansi::warning() + "注意: " + text + Ansi::reset() + "\n");
}

// ============================================================================
// 状态栏
// ============================================================================

void TerminalGUI::renderStatusBar(const std::string& mode, int tokens,
                                   const std::string& extra) {
    std::string bar = Ansi::statusBar() + " " + mode;
    if (!extra.empty()) bar += " | " + extra;
    bar += " | " + std::to_string(tokens) + " tokens ";
    bar += Ansi::reset() + "\n";
    out_.write(bar);
}

// ============================================================================
// 进度指示器
// ============================================================================

void TerminalGUI::startSpinner(const std::string& label) {
    spinner_active_ = true;
    spinner_frame_ = 0;
    out_.write(Ansi::thinking() + Ansi::spinner(0) + " " + label + "... "
               + Ansi::reset());
}

void TerminalGUI::tickSpinner() {
    if (!spinner_active_) return;
    ++spinner_frame_;
    // 在实际使用中通过 \r 回到行首更新
    out_.write("\r" + Ansi::thinking() + Ansi::spinner(spinner_frame_) + " "
               + Ansi::reset());
}

void TerminalGUI::stopSpinner() {
    spinner_active_ = false;
    out_.write("\r" + Ansi::clearLine());
}

// ============================================================================
// Markdown 渲染
// ============================================================================

std::string TerminalGUI::renderMarkdown(const std::string& text) const {
    std::string result = text;

    // 粗体 **text** → ANSI 粗体
    result = renderBold(result);
    // 斜体 *text* → ANSI 斜体
    result = renderItalic(result);

    return result;
}

std::string TerminalGUI::renderBold(const std::string& text) {
    std::regex bold_re(R"(\*\*(.+?)\*\*)");
    return std::regex_replace(text, bold_re,
        Ansi::bold() + "$1" + Ansi::reset() + Ansi::assistant());
}

std::string TerminalGUI::renderItalic(const std::string& text) {
    std::regex italic_re(R"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*))");
    return std::regex_replace(text, italic_re,
        Ansi::italic() + "$1" + Ansi::reset() + Ansi::assistant());
}

std::string TerminalGUI::renderCodeBlock(const std::string& text) {
    // 简化：用灰色背景标记代码块
    return Ansi::fgBrightBlack() + text + Ansi::reset();
}

// ============================================================================
// 命令历史
// ============================================================================

void TerminalGUI::addToHistory(const std::string& line) {
    if (line.empty()) return;
    if (history_.empty() || history_.back() != line) {
        history_.push_back(line);
        if (history_.size() > 100) history_.pop_front();
    }
}

// ============================================================================
// 标题/分隔线
// ============================================================================

void TerminalGUI::writeTitle(const std::string& title) {
    out_.write("\n" + Ansi::title() + title + Ansi::reset() + "\n");
    writeSeparator();
}

void TerminalGUI::writeSeparator() {
    out_.write(Ansi::hLine(60) + "\n");
}
