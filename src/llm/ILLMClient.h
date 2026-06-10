#pragma once

#include "config/AppConfig.h"
#include "llm/Message.h"

#include <functional>
#include <string>
#include <vector>

namespace llm {

/// 流式调用的实时回调接口。
/// 调用方（Agent / StreamDisplay）通过这些回调获得逐 token 的输出。
struct StreamCallbacks {
    std::function<void(const std::string& delta)> on_content;
    std::function<void(const std::string& delta)> on_reasoning;
    std::function<void()> on_tool_call_start;
    std::function<void(const LLMResponse& response)> on_complete;
    std::function<void(const std::string& error)> on_error;
};

/// LLM 客户端抽象接口 — 解耦 Agent 与具体的 HTTP 实现。
///
/// Agent 通过此接口调用 LLM，不感知底层是 HTTP/SSE/Mock。
/// 新增 LLM 后端只需实现此接口，无需修改 Agent 代码。
///
/// 实现类: LLMClient（HTTP + SSE）
class ILLMClient {
public:
    virtual ~ILLMClient() = default;

    /// 流式调用：实时通过 callbacks 输出增量。
    virtual LLMResponse chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = "",
        StreamCallbacks callbacks = {}) = 0;

    /// 非流式调用：等待完整响应后返回。
    virtual LLMResponse chatNonStreaming(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = "") = 0;

    /// 返回当前使用的 ProviderConfig（只读）。
    virtual const ProviderConfig& config() const = 0;
};

} // namespace llm
