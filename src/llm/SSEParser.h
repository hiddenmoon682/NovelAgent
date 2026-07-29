#pragma once

#include <functional>
#include <string>
#include <nlohmann/json_fwd.hpp>
#include "llm/StreamingTypes.h"

namespace llm {

// SSE (Server-Sent Events) 流式解析器。
// 纯协议解析：将 SSE 文本流转换为 `StreamChunk` 对象，通过单回调输出。
// 不持有跨 chunk 状态，不负责 tool_calls 合并——这些由 `StreamAccumulator` 处理。
class SSEParser {
public:
    using ChunkCallback = std::function<void(const StreamChunk& chunk)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    // 注册 chunk 回调（每解析出一个完整 SSE 事件时触发）。
    //
    // @param cb 回调函数，在 feed() 的调用线程中同步触发；可为空（忽略事件）。
    void setOnChunk(ChunkCallback cb) { on_chunk_ = std::move(cb); }

    // 注册解析错误回调（JSON 解析失败或 SSE 流内嵌错误事件时触发）。
    //
    // @param cb 回调函数，携带错误描述；可为空（忽略错误）。
    void setOnError(ErrorCallback cb) { on_error_ = std::move(cb); }

    // 喂入原始 SSE 数据块。
    //
    // 数据可能包含多个完整事件、不完整事件或两者混合；
    // 不完整部分缓存到下次 feed() 拼接。
    //
    // @param data 原始 SSE 文本片段（HTTP content_receiver 收到的字节串）。
    void feed(const std::string& data);

    // 重置内部缓冲，准备解析新的流（已注册的回调保留）。
    void reset();

private:
    std::string buffer_; // 跨 feed() 调用的不完整数据缓冲

    ChunkCallback on_chunk_;
    ErrorCallback on_error_;

    // 解析一个完整的 SSE 事件（不含分隔符）
    void processEvent(const std::string& eventText);

    // 解析 JSON chunk，构建 StreamChunk 对象并通过 on_chunk_ 发出
    void processChunk(const nlohmann::json& j);
};

} // namespace llm
