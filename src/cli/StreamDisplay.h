#pragma once

#include "cli/IOutputChannel.h"
#include "llm/ILLMClient.h"

/// 流式输出显示 — 将 LLM 流式回调包装为终端友好的输出。
/// 通过 IOutputChannel 注入，支持重定向到控制台/文件/WebSocket。
class StreamDisplay {
public:
    /// @param out  输出通道（默认 = 标准控制台）
    static llm::StreamCallbacks create(IOutputChannel& out);
};
