#pragma once

#include "llm/LLMClient.h"  // StreamCallbacks
#include "llm/SSEParser.h"
#include "llm/StreamAccumulator.h"

namespace llm {

/// 流式管道 — 将 SSE 文本流转换为完整 LLMResponse。
///
/// 内部组合 SSEParser（解析） + StreamAccumulator（累积），
/// 并透明转发回调给外部 StreamCallbacks。
///
/// 使用示例：
///   StreamingPipeline pipeline;
///   pipeline.setCallbacks(callbacks);
///
///   // 在 HTTP content_receiver 中逐块喂入原始 SSE 数据
///   pipeline.feed(rawSseData);
///
///   if (pipeline.hasError()) { throw ...; }
///   if (!pipeline.completed()) { throw ...; }
///   return pipeline.response();
///
/// 注意：StreamingPipeline 不负责 HTTP 传输——仅处理 SSE 数据流。
class StreamingPipeline {
public:
    /// 构造时自动连接内部管道（SSEParser → StreamAccumulator → 回调转发）。
    /// setCallbacks() 可在构造后任意时刻调用——lambda 捕获 this，
    /// 回调触发时总是读取最新的 callbacks_。
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

    /// 设置外部回调（转发给调用方——Agent / StreamDisplay）。
    /// 可在首次 feed() 之前或之后调用。
    void setCallbacks(const StreamCallbacks& cb) { callbacks_ = cb; }

    /// 喂入原始 SSE 数据块。
    /// 内部自动完成：SSE 文本解析 → StreamChunk → 累积 → 回调转发。
    void feed(const std::string& data) { parser_.feed(data); }

    // ── 状态查询 ──

    /// 流是否已正常结束（收到 finish_reason 或 [DONE] 信号）。
    bool completed() const { return completed_; }

    /// 是否有解析错误。
    bool hasError() const { return !error_.empty(); }

    /// 获取错误描述（如果没有错误则返回空字符串）。
    const std::string& error() const { return error_; }

    /// 获取累积完成的响应（仅在 completed() == true 时有效）。
    const LLMResponse& response() const { return response_; }

    /// 重置所有状态，准备处理新的 SSE 流。
    void reset() {
        parser_.reset();
        accumulator_.reset();
        response_ = LLMResponse{};
        completed_ = false;
        tool_call_seen_ = false;
        error_.clear();
    }

private:
    SSEParser parser_;
    StreamAccumulator accumulator_;
    StreamCallbacks callbacks_;
    LLMResponse response_;
    bool completed_ = false;
    bool tool_call_seen_ = false;
    std::string error_;
};

} // namespace llm
