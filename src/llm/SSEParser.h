#pragma once

#include <functional>
#include <string>
#include <nlohmann/json_fwd.hpp>
#include "llm/StreamingTypes.h"

namespace llm {

/// SSE (Server-Sent Events) 流式解析器。
/// 纯协议解析：将 SSE 文本流转换为 `StreamChunk` 对象，通过单回调输出。
/// 不持有跨 chunk 状态，不负责 tool_calls 合并——这些由 `StreamAccumulator` 处理。
class SSEParser {
public:
    using ChunkCallback = std::function<void(const StreamChunk& chunk)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    void setOnChunk(ChunkCallback cb) { on_chunk_ = std::move(cb); }
    void setOnError(ErrorCallback cb) { on_error_ = std::move(cb); }

    /// 喂入原始 SSE 数据块。数据可能包含多个完整事件、不完整事件或两者混合。
    void feed(const std::string& data);

    /// 重置内部缓冲，准备解析新的流。
    void reset();

private:
    std::string buffer_; // 跨 feed() 调用的不完整数据缓冲

    ChunkCallback on_chunk_;
    ErrorCallback on_error_;

    /// 解析一个完整的 SSE 事件（不含分隔符）
    void processEvent(const std::string& eventText);

    /// 解析 JSON chunk，构建 StreamChunk 对象并通过 on_chunk_ 发出
    void processChunk(const nlohmann::json& j);
};

} // namespace llm
