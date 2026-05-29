#pragma once

#include <functional>
#include <map>
#include "llm/Message.h"

namespace llm {

/// 流式响应累积器 — 接收 StreamChunk，跨 chunk 合并后产出完整 LLMResponse。
///
/// 职责：
/// - 文本增量拼接（content_delta / reasoning_delta 跨 chunk 连接）
/// - 工具调用按 index 累积（arguments 跨 chunk 拼接）
/// - 首个 chunk 捕获 id / model / created 元数据
/// - 流结束时（finish_reason 非空 或 is_end）触发 on_done 回调
///
/// 使用方式：
///   StreamAccumulator acc;
///   acc.setOnDone([](const LLMResponse& r) { /* 处理完整响应 */ });
///   // 在 SSEParser 的 onChunk 回调中：
///   acc.feed(chunk);
class StreamAccumulator {
public:
    using DoneCallback = std::function<void(const LLMResponse& response)>;

    /// 喂入一个流式 chunk，内部累积状态。
    /// 当 chunk 携带 finish_reason 或 is_end 标记时，自动触发 on_done 回调。
    void feed(const StreamChunk& chunk);

    /// 重置所有累积状态，准备处理新的流。
    void reset();

    /// 注册流结束回调（产出完整 LLMResponse 时触发）。
    void setOnDone(DoneCallback cb) { on_done_ = std::move(cb); }

private:
    LLMResponse response_;                          // 累积中的响应
    std::map<int, ToolCall> pending_tool_calls_;    // 按 index 累积中的 tool_call
    bool id_captured_ = false;                      // 首个 chunk 的元数据是否已捕获

    DoneCallback on_done_;

    /// 检查是否满足流结束条件，满足则触发 on_done
    void checkComplete(const StreamChunk& chunk);
};

} // namespace llm
