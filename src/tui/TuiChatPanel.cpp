/// TuiChatPanel 实现 — Claude Code 风格消息渲染。

#include "tui/TuiChatPanel.h"

#include <ftxui/dom/elements.hpp>
#include <sstream>

using namespace ftxui;

// ── 颜色常量 ──
static const Color kUserColor     = Color::Cyan;
static const Color kUserCircle    = Color::Blue;
static const Color kAssistantColor = Color::White;
static const Color kThinkingColor = Color::GrayDark;
static const Color kToolColor     = Color::Yellow;
static const Color kToolDim       = Color::GrayDark;
static const Color kErrorColor    = Color::Red;
static const Color kSystemColor   = Color::GrayDark;

TuiChatPanel::TuiChatPanel() = default;

void TuiChatPanel::startAssistantMessage() {
    messages_.push_back({MessageEntry::Role::Assistant, "", "", false});
}

void TuiChatPanel::appendContent(const std::string& delta) {
    if (messages_.empty()) startAssistantMessage();
    messages_.back().content += delta;
}

void TuiChatPanel::appendThinking(const std::string& delta) {
    // 如果最后一条不是思考消息，新建一条
    if (messages_.empty() || messages_.back().role != MessageEntry::Role::Thinking) {
        messages_.push_back({MessageEntry::Role::Thinking, "", "", false});
    }
    messages_.back().content += delta;
}

void TuiChatPanel::finishMessage() {
    if (!messages_.empty()) messages_.back().finished = true;
}

void TuiChatPanel::appendUserMessage(const std::string& text) {
    messages_.push_back({MessageEntry::Role::User, text, "", true});
}

void TuiChatPanel::appendError(const std::string& text) {
    messages_.push_back({MessageEntry::Role::Error, text, "", true});
}

void TuiChatPanel::appendSystemMessage(const std::string& text) {
    messages_.push_back({MessageEntry::Role::System, text, "", true});
}

void TuiChatPanel::appendToolCall(const std::string& toolName) {
    messages_.push_back({MessageEntry::Role::ToolCall, "", toolName, true});
}

const MessageEntry* TuiChatPanel::currentMessage() const {
    return messages_.empty() ? nullptr : &messages_.back();
}

// ── Markdown 解析 ──

Elements TuiChatPanel::parseMarkdown(const std::string& rawText) {
    Elements result;
    std::istringstream stream(rawText);
    std::string line;
    bool inCodeBlock = false;

    while (std::getline(stream, line)) {
        // 代码块 ```...```
        if (line.starts_with("```")) {
            inCodeBlock = !inCodeBlock;
            continue;
        }
        if (inCodeBlock) {
            result.push_back(text("  " + line) | dim | bgcolor(Color::GrayDark));
            continue;
        }

        // 空行 → 段落间距（用空行占位符，保证视觉分隔）
        if (line.empty()) {
            result.push_back(emptyElement() | size(HEIGHT, EQUAL, 1));
            continue;
        }

        // 处理行内 Markdown：**粗体** *斜体* `代码`
        Elements segments;
        std::string segment;
        size_t i = 0;

        auto pushText = [&](const std::string& s) {
            if (!s.empty()) segments.push_back(text(s));
        };

        auto pushBold = [&](const std::string& s) {
            segments.push_back(text(s) | bold);
        };

        auto pushItalic = [&](const std::string& s) {
            segments.push_back(text(s) | dim);  // FTXUI 无 italic 装饰，用 dim 近似
        };

        auto pushCode = [&](const std::string& s) {
            segments.push_back(text(s) | dim | bgcolor(Color::GrayDark));
        };

        while (i < line.size()) {
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*') {
                pushText(segment); segment.clear();
                size_t end = line.find("**", i + 2);
                if (end != std::string::npos) {
                    pushBold(line.substr(i + 2, end - i - 2));
                    i = end + 2;
                } else {
                    segment = "**"; i += 2;
                }
            } else if (line[i] == '*' && (i == 0 || line[i-1] != '*') && i + 1 < line.size() && line[i+1] != '*') {
                pushText(segment); segment.clear();
                size_t end = line.find('*', i + 1);
                if (end != std::string::npos) {
                    pushItalic(line.substr(i + 1, end - i - 1));
                    i = end + 1;
                } else {
                    segment += line[i]; i++;
                }
            } else if (line[i] == '`') {
                pushText(segment); segment.clear();
                size_t end = line.find('`', i + 1);
                if (end != std::string::npos) {
                    pushCode(line.substr(i + 1, end - i - 1));
                    i = end + 1;
                } else {
                    segment += '`'; i++;
                }
            } else {
                segment += line[i]; i++;
            }
        }
        pushText(segment);

        if (segments.size() == 1) {
            result.push_back(segments[0]);
        } else {
            result.push_back(hbox(std::move(segments)));
        }
    }

    return result;
}

// ── 各角色消息渲染 ──

Element TuiChatPanel::renderUser(const MessageEntry& msg) {
    auto circle = text("⏺") | color(kUserCircle);
    auto body = paragraph(msg.content) | color(kUserColor);
    return hbox({
        circle,
        text(" "),
        body,
    });
}

Element TuiChatPanel::renderAssistant(const MessageEntry& msg) {
    auto parts = parseMarkdown(msg.content);
    if (parts.empty()) return text("");

    // 每个段落后面加空行间距
    Elements spaced;
    for (auto& p : parts) {
        spaced.push_back(std::move(p));
    }
    return vbox(std::move(spaced)) | color(kAssistantColor);
}

Element TuiChatPanel::renderThinking(const MessageEntry& msg) {
    auto header = hbox({
        text("⏺") | color(kThinkingColor),
        text(" 思考中…") | dim | color(kThinkingColor),
    });
    auto body = paragraph(msg.content) | dim | color(kThinkingColor);
    return vbox({
        header,
        body | size(WIDTH, GREATER_THAN, 60),
    });
}

Element TuiChatPanel::renderToolCall(const MessageEntry& msg) {
    auto circle = text("⏺") | color(kToolColor);
    auto toolLabel = text(" ◇ " + msg.toolName) | color(kToolColor) | bold;
    return hbox({
        circle,
        toolLabel,
        text(" 执行中…") | dim | color(kToolDim),
    });
}

Element TuiChatPanel::renderError(const MessageEntry& msg) {
    auto circle = text("⏺") | color(kErrorColor);
    auto body = paragraph(msg.content) | color(kErrorColor);
    return hbox({
        circle,
        text(" "),
        body,
    });
}

Element TuiChatPanel::renderSystem(const MessageEntry& msg) {
    return paragraph(msg.content) | dim | color(kSystemColor);
}

// ── 主渲染 ──

Element TuiChatPanel::render() const {
    Elements elements;

    for (size_t i = 0; i < messages_.size(); ++i) {
        const auto& msg = messages_[i];

        Element rendered;
        switch (msg.role) {
        case MessageEntry::Role::User:
            rendered = renderUser(msg); break;
        case MessageEntry::Role::Assistant:
            rendered = renderAssistant(msg); break;
        case MessageEntry::Role::Thinking:
            rendered = renderThinking(msg); break;
        case MessageEntry::Role::ToolCall:
            rendered = renderToolCall(msg); break;
        case MessageEntry::Role::Error:
            rendered = renderError(msg); break;
        case MessageEntry::Role::System:
            rendered = renderSystem(msg); break;
        }

        elements.push_back(rendered);

        // 消息间间距：不同角色间加空行，同角色工具/思考不加
        if (i + 1 < messages_.size()) {
            auto& next = messages_[i + 1];
            bool sameGroup = (msg.role == MessageEntry::Role::Thinking ||
                              msg.role == MessageEntry::Role::ToolCall) &&
                             (next.role == MessageEntry::Role::Thinking ||
                              next.role == MessageEntry::Role::Assistant);
            if (!sameGroup) {
                elements.push_back(emptyElement() | size(HEIGHT, EQUAL, 1));
            }
        }
    }

    if (elements.empty()) {
        return vbox({
            emptyElement() | size(HEIGHT, EQUAL, 3),
            text(" NovelAgent") | bold | color(Color::Cyan) | center,
            text(" 输入消息开始写作，输入 /help 查看命令") | dim | center,
            emptyElement() | size(HEIGHT, EQUAL, 1),
        }) | center;
    }

    return vbox(std::move(elements));
}
