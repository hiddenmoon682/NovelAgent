#pragma once

// 流式输出显示 — Phase 5 增强版。
//
// 提供基于 ANSI 主题的 LLM 流式回调包装：
// - 内容输出：绿色（助手回复）
// - 思考链：暗灰色
// - 工具调用：灰色标签
// - Token 统计：灰色
// - 错误：红色
//
// 通过 IOutputChannel 注入，支持重定向到控制台/文件/WebSocket。

#include "cli/AnsiTerminal.h"
#include "cli/IOutputChannel.h"
#include "llm/ILLMClient.h"

#include <chrono>

class StreamDisplay {
public:
    // 创建流式回调（Phase 5 增强版）。
    // out      输出通道
    // show_token_stats  是否显示 token 统计（默认 true）
    static llm::StreamCallbacks create(IOutputChannel& out,
                                       bool show_token_stats = true);

    // 创建带状态感知的回调（供 Agent 使用）。
    // out          输出通道
    // state_signal 状态信号（Agent 在回调中更新状态机）
    static llm::StreamCallbacks createWithState(
        IOutputChannel& out,
        std::function<void()> on_content_start,
        std::function<void()> on_tool_start,
        std::function<void()> on_complete);
};
