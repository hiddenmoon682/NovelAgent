#pragma once

/// ANSI 终端工具 — 颜色、样式、光标控制。
///
/// Phase 5 架构改进：集中管理所有 ANSI 转义码，避免散落在各组件中。
/// Windows 上自动启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING。
///
/// 使用示例:
///   out.write(Ansi::fgGreen() + "助手回复" + Ansi::reset());

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Ansi {

// ============================================================================
// 初始化
// ============================================================================

/// 在 Windows 上启用 ANSI 转义码支持。Linux/macOS 无需调用。
/// 应在程序启动时调用一次。
inline void enableWindowsAnsi() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
}

// ============================================================================
// 基础控制
// ============================================================================

inline std::string reset()      { return "\033[0m"; }
inline std::string bold()       { return "\033[1m"; }
inline std::string dim()        { return "\033[2m"; }
inline std::string italic()     { return "\033[3m"; }
inline std::string underline()  { return "\033[4m"; }

// ============================================================================
// 前景色（标准 16 色）
// ============================================================================

inline std::string fgBlack()   { return "\033[30m"; }
inline std::string fgRed()     { return "\033[31m"; }
inline std::string fgGreen()   { return "\033[32m"; }
inline std::string fgYellow()  { return "\033[33m"; }
inline std::string fgBlue()    { return "\033[34m"; }
inline std::string fgMagenta() { return "\033[35m"; }
inline std::string fgCyan()    { return "\033[36m"; }
inline std::string fgWhite()   { return "\033[37m"; }

// ── 亮色 ──
inline std::string fgBrightBlack()  { return "\033[90m"; }
inline std::string fgBrightRed()    { return "\033[91m"; }
inline std::string fgBrightGreen()  { return "\033[92m"; }
inline std::string fgBrightYellow() { return "\033[93m"; }
inline std::string fgBrightBlue()   { return "\033[94m"; }
inline std::string fgBrightMagenta(){ return "\033[95m"; }
inline std::string fgBrightCyan()   { return "\033[96m"; }
inline std::string fgBrightWhite()  { return "\033[97m"; }

// ============================================================================
// 背景色
// ============================================================================

inline std::string bgRed()    { return "\033[41m"; }
inline std::string bgGreen()  { return "\033[42m"; }
inline std::string bgYellow() { return "\033[43m"; }
inline std::string bgBlue()   { return "\033[44m"; }

// ============================================================================
// 光标控制
// ============================================================================

inline std::string cursorUp(int n = 1)    { return "\033[" + std::to_string(n) + "A"; }
inline std::string cursorDown(int n = 1)  { return "\033[" + std::to_string(n) + "B"; }
inline std::string cursorRight(int n = 1) { return "\033[" + std::to_string(n) + "C"; }
inline std::string cursorLeft(int n = 1)  { return "\033[" + std::to_string(n) + "D"; }
inline std::string clearLine()            { return "\033[2K"; }
inline std::string clearScreen()          { return "\033[2J\033[H"; }
inline std::string saveCursor()           { return "\033[s"; }
inline std::string restoreCursor()        { return "\033[u"; }
inline std::string hideCursor()           { return "\033[?25l"; }
inline std::string showCursor()           { return "\033[?25h"; }

// ============================================================================
// 语义化颜色（Phase 5 统一主题）
// ============================================================================

/// 助手回复 — 绿色
inline std::string assistant()  { return fgGreen(); }

/// 用户输入 — 蓝色加粗
inline std::string userInput()  { return bold() + fgBlue(); }

/// 工具调用 — 灰色/暗色
inline std::string toolCall()   { return fgBrightBlack(); }

/// 思考链 — 暗灰色
inline std::string thinking()   { return dim() + fgBrightBlack(); }

/// 错误 — 红色
inline std::string error()      { return fgRed(); }

/// 警告 — 黄色
inline std::string warning()    { return fgYellow(); }

/// 成功/完成 — 亮绿色
inline std::string success()    { return fgBrightGreen(); }

/// 信息 — 青色
inline std::string info()       { return fgCyan(); }

/// 标题 — 亮白色加粗
inline std::string title()      { return bold() + fgBrightWhite(); }

/// 状态栏 — 白色文字 + 蓝色背景
inline std::string statusBar()  { return bgBlue() + fgWhite(); }

// ============================================================================
// 终端 GUI 组件
// ============================================================================

/// 渲染水平分隔线。
inline std::string hLine(int width = 60, char c = '─') {
    return dim() + std::string(width, c) + reset() + "\n";
}

/// 渲染带颜色的标签 [标签]。
inline std::string label(const std::string& text, const std::string& color) {
    return color + "[" + text + "]" + reset();
}

/// 渲染状态指示器（如 "Thinking..." 右侧的旋转动画文本）。
inline std::string spinner(int frame) {
    static const char* frames[] = {"|", "/", "-", "\\"};
    return std::string(frames[frame % 4]);
}

} // namespace Ansi
