#include "llm/StreamAccumulator.h"

namespace llm {

// ===========================================================================
// feed — 接收 StreamChunk，累积状态
// ===========================================================================

void StreamAccumulator::feed(const StreamChunk& chunk)
{
    // 首个携带有效元数据的 chunk → 捕获 id / model / created
    if (!id_captured_) {
        if (!chunk.id.empty() || !chunk.model.empty() || chunk.created > 0) {
            response_.id = chunk.id;
            response_.model = chunk.model;
            response_.created = chunk.created;
            id_captured_ = true;
        }
    }

    // 文本增量拼接
    if (!chunk.content_delta.empty()) {
        response_.content += chunk.content_delta;
    }
    if (!chunk.reasoning_delta.empty()) {
        response_.reasoning_content += chunk.reasoning_delta;
    }

    // 工具调用增量 — 按 index 累积，arguments 跨 chunk 拼接
    for (const auto& tcd : chunk.tool_call_deltas) {
        auto& pending = pending_tool_calls_[tcd.index];

        // id / type / function_name 只在首个 chunk 携带
        if (!tcd.id.empty()) {
            pending.id = tcd.id;
        }
        if (!tcd.type.empty()) {
            pending.type = tcd.type;
        }
        if (!tcd.function_name.empty()) {
            pending.function_name = tcd.function_name;
        }
        // arguments 跨 chunk 增量返回，必须拼接
        if (!tcd.arguments.empty()) {
            pending.arguments += tcd.arguments;
        }
    }

    // usage — 末个 chunk 覆盖
    if (chunk.usage.total_tokens > 0) {
        response_.prompt_tokens = chunk.usage.prompt_tokens;
        response_.completion_tokens = chunk.usage.completion_tokens;
        response_.total_tokens = chunk.usage.total_tokens;
        response_.cached_tokens = chunk.usage.cached_tokens;
        response_.reasoning_tokens = chunk.usage.reasoning_tokens;
    }

    checkComplete(chunk);
}

// ===========================================================================
// checkComplete — 流结束时组装最终 LLMResponse
// ===========================================================================

void StreamAccumulator::checkComplete(const StreamChunk& chunk)
{
    // 已完成则忽略后续事件（防止 [DONE] 覆盖 finish_reason）
    if (completed_) return;

    // finish_reason 为空 且 is_end 为 false → 继续等待
    if (chunk.finish_reason.empty() && !chunk.is_end) {
        return;
    }

    completed_ = true;

    // finish_reason 取其值；is_end 但无 finish_reason 时默认 "stop"
    response_.finish_reason = chunk.finish_reason.empty() ? "stop" : chunk.finish_reason;

    // 将累积的 tool_calls 按 index 顺序移入响应
    for (auto& [idx, tc] : pending_tool_calls_) {
        response_.tool_calls.push_back(std::move(tc));
    }
    pending_tool_calls_.clear();

    if (on_done_) {
        on_done_(response_);
    }
}

// ===========================================================================
// reset
// ===========================================================================

void StreamAccumulator::reset()
{
    response_ = LLMResponse{};
    pending_tool_calls_.clear();
    id_captured_ = false;
    completed_ = false;
}

} // namespace llm
