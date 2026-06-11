#pragma once

/// FTXUI TUI 聊天面板 — Claude Code 风格的流式消息显示。
///
/// 视觉规范：
/// - 用户消息：蓝色 ⏺ 前缀，文字高亮
/// - 助手消息：白色正文，无前缀，支持 Markdown 渲染
/// - 思考链：暗灰色 ⏺"思考中…"，内容缩进、折叠感
/// - 工具调用：黄色 ⏺"◇ 工具名"，参数灰色
/// - 错误消息：红色 ⏺ 前缀
/// - 系统消息：暗色斜体

#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

/// 单条聊天消息。
struct MessageEntry {
    enum class Role { User, Assistant, System, Error, Thinking, ToolCall };
    Role role = Role::User;
    std::string content;    ///< 当前累积的文本内容
    std::string toolName;   ///< 工具名（仅 ToolCall 角色）
    bool finished = false;  ///< 消息是否已接收完整
};

/// 聊天面板组件。
class TuiChatPanel {
public:
    TuiChatPanel();

    // ── 消息构建 ──

    void startAssistantMessage();
    void appendContent(const std::string& delta);
    void appendThinking(const std::string& delta);
    void finishMessage();
    void appendUserMessage(const std::string& text);
    void appendError(const std::string& text);
    void appendSystemMessage(const std::string& text);
    void appendToolCall(const std::string& toolName);

    /// 获取当前消息引用（供外部获取流式状态）。
    const MessageEntry* currentMessage() const;

    /// 渲染消息列表为 FTXUI Element。
    ftxui::Element render() const;

private:
    std::vector<MessageEntry> messages_;

    /// 渲染单条用户消息。
    static ftxui::Element renderUser(const MessageEntry& msg);
    /// 渲染单条助手消息（含 Markdown 处理）。
    static ftxui::Element renderAssistant(const MessageEntry& msg);
    /// 渲染思考链。
    static ftxui::Element renderThinking(const MessageEntry& msg);
    /// 渲染工具调用。
    static ftxui::Element renderToolCall(const MessageEntry& msg);
    /// 渲染错误。
    static ftxui::Element renderError(const MessageEntry& msg);
    /// 渲染系统消息。
    static ftxui::Element renderSystem(const MessageEntry& msg);

    /// 简单 Markdown 渲染：**粗体** / *斜体* / `代码`。
    static ftxui::Elements parseMarkdown(const std::string& text);
};
