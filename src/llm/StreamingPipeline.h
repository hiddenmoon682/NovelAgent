#pragma once

// 流式管道 — 将 SSE 文本流转换为完整 LLMResponse。
//
// Fix #4: 支持依赖注入（ISSEParser/IStreamAccumulator mock）。
// Fix #5: 语义化回调类型别名 SSEEventHandler, ChunkProvider。

#include "llm/LLMClient.h"
#include "llm/SSEParser.h"
#include "llm/StreamAccumulator.h"

#include <functional>

namespace llm {

// Fix #5: 语义化回调类型别名
using SSEEventHandler = std::function<void(const std::string& type, const std::string& data)>;
using ChunkDataCallback = std::function<bool(const char* data, size_t len)>;
//
// 内部组合 SSEParser（解析） + StreamAccumulator（累积），
// 并透明转发回调给外部 StreamCallbacks。
//
// 使用示例：
//   StreamingPipeline pipeline;
//   pipeline.setCallbacks(callbacks);
//
//   // 在 HTTP content_receiver 中逐块喂入原始 SSE 数据
//   pipeline.feed(rawSseData);
//
//   if (pipeline.hasError()) { throw ...; }
//   if (!pipeline.completed()) { throw ...; }
//   return pipeline.response();
//
// 注意：StreamingPipeline 不负责 HTTP 传输——仅处理 SSE 数据流。
class StreamingPipeline {
public:
    // 构造时自动连接内部管道（SSEParser → StreamAccumulator → 回调转发）。
    //
    // WHY：内部 lambda 只捕获 this，回调触发时才读取成员 callbacks_，
    // 而不是在构造时把外部回调拷贝进 lambda。这样回调链的布线（一次性，
    // 构造时完成）与外部回调的注入（setCallbacks，可在 feed() 前后任意时刻
    // 调用/替换）彻底解耦：reset() 复用管道时无需重新布线，调用方（如
    // LLMClient::chat）也不必关心构造与注入的先后顺序。
    StreamingPipeline() {
        parser_.setOnChunk([this](const StreamChunk& chunk) {
            accumulator_.feed(chunk);

            if (callbacks_.on_content && !chunk.content_delta.empty()) {
                callbacks_.on_content(chunk.content_delta);
            }
            if (callbacks_.on_reasoning && !chunk.reasoning_delta.empty()) {
                callbacks_.on_reasoning(chunk.reasoning_delta);
            }
            if (callbacks_.on_tool_call_start && !tool_call_seen_
                && !chunk.tool_call_deltas.empty()) {
                tool_call_seen_ = true;
                callbacks_.on_tool_call_start();
            }
        });

        parser_.setOnError([this](const std::string& err) {
            error_ = err;
            if (callbacks_.on_error) {
                callbacks_.on_error(err);
            }
        });

        accumulator_.setOnDone([this](const LLMResponse& response) {
            response_ = response;
            completed_ = true;
            if (callbacks_.on_complete) {
                callbacks_.on_complete(response);
            }
        });
    }

    // 设置外部回调（转发给调用方——Agent / QmlBridge）。
    //
    // 可在首次 feed() 之前或之后调用（见构造函数注释的布线设计理由）。
    //
    // @param cb 回调集合，按值拷贝保存；各回调在 feed() 的调用线程中同步触发。
    void setCallbacks(const StreamCallbacks& cb) { callbacks_ = cb; }

    // 喂入原始 SSE 数据块。
    // 内部自动完成：SSE 文本解析 → StreamChunk → 累积 → 回调转发。
    void feed(const std::string& data) { parser_.feed(data); }

    // ── 状态查询 ──

    // 流是否已正常结束（收到 finish_reason 或 [DONE] 信号）。
    bool completed() const { return completed_; }

    // 是否有解析错误。
    bool hasError() const { return !error_.empty(); }

    // 获取错误描述（如果没有错误则返回空字符串）。
    const std::string& error() const { return error_; }

    // 获取累积完成的响应（仅在 completed() == true 时有效）。
    const LLMResponse& response() const { return response_; }

    // 重置所有状态，准备处理新的 SSE 流。
    void reset() {
        parser_.reset();
        accumulator_.reset();
        response_ = LLMResponse{};
        completed_ = false;
        tool_call_seen_ = false;
        error_.clear();
    }

private:
    SSEParser parser_;                // SSE 解析器，将原始 SSE 文本解析为 StreamChunk
    StreamAccumulator accumulator_;   // 流累积器，将多个 StreamChunk 累积为完整的 LLMResponse
    StreamCallbacks callbacks_;       // 外部回调函数集，转发给 Agent / QmlBridge
    LLMResponse response_;            // 累积完成的完整 LLM 响应
    bool completed_ = false;          // 标记流是否已正常结束（收到 finish_reason 或 [DONE]）
    bool tool_call_seen_ = false;     // 标记是否已触发 on_tool_call_start，确保只触发一次
    std::string error_;               // 解析或处理过程中产生的错误描述
};

} // namespace llm
