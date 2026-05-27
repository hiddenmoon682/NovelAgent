#pragma once

#include <functional>
#include <string>
#include "llm/Message.h"

namespace llm {

/// SSE (Server-Sent Events) 流式解析器。
/// 解析 OpenAI 兼容 API 的流式响应，提取文本增量和工具调用增量。
/// 通过回调将解析结果传递给 LLMClient，不持有对话状态。
class SSEParser {
public:
    using TokenCallback    = std::function<void(const std::string& token)>;
    using ToolCallCallback = std::function<void(const ToolCall& tc)>;
    using DoneCallback     = std::function<void()>;
    using ErrorCallback    = std::function<void(const std::string& error)>;

    void setOnToken(TokenCallback cb)    { on_token_ = std::move(cb); }
    void setOnToolCall(ToolCallCallback cb) { on_tool_call_ = std::move(cb); }
    void setOnDone(DoneCallback cb)      { on_done_ = std::move(cb); }
    void setOnError(ErrorCallback cb)    { on_error_ = std::move(cb); }

    /// 喂入原始 SSE 数据块。数据可能包含多个完整事件、不完整事件或两者混合。
    void feed(const std::string& data);

    /// 重置所有内部状态，准备解析新的流。
    void reset();

private:
    std::string buffer_; // 跨 feed() 调用的不完整数据缓冲

    TokenCallback    on_token_;
    ToolCallCallback on_tool_call_;
    DoneCallback     on_done_;
    ErrorCallback    on_error_;

    /// 解析一个完整的 SSE 事件（不含分隔符）
    void processEvent(const std::string& eventText);

    /// 解析 JSON chunk，提取 delta 中的 content 和 tool_calls
    void processChunk(const nlohmann::json& j);
};

} // namespace llm
