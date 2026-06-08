#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace llm {

// ============================================================================
// 流式响应中间类型 — SSE 解析器输出，StreamAccumulator 消费
// ============================================================================

/// 流式响应中单个 tool_call 的增量字段。
/// 一个 chunk 可含多个 ToolCallDelta（每个 index 一个），arguments 为增量片段。
struct ToolCallDelta {
    int index = 0;             // tool_call 在数组中的索引（用于跨 chunk 合并）
    std::string id;            // 首个 chunk 携带
    std::string type;          // 通常为 "function"
    std::string function_name; // 首个 chunk 携带
    std::string arguments;     // 增量片段，非完整 JSON，需跨 chunk 拼接
};

/// 流式响应的 token 统计信息（末个 chunk 携带）。
struct UsageInfo {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    int cached_tokens = 0;      // prompt_tokens_details.cached_tokens
    int reasoning_tokens = 0;   // completion_tokens_details.reasoning_tokens
};

/// 流式响应中单个 chunk 的完整表示（SSE 解析器的输出单元）。
/// 所有增量字段与 OpenAI/DeepSeek 流式 delta 结构一一对应。
struct StreamChunk {
    std::string id;                          // 响应 ID（首个 chunk 携带）
    std::string model;                       // 模型名（首个 chunk 携带）
    int64_t created = 0;                     // Unix 时间戳（首个 chunk 携带）
    std::string content_delta;               // 文本增量
    std::string reasoning_delta;             // 思维链增量（DeepSeek thinking 模式）
    std::vector<ToolCallDelta> tool_call_deltas; // 工具调用增量（同一 chunk 可含多个 index）
    std::string finish_reason;               // 结束原因（末个 chunk 携带）
    UsageInfo usage;                         // token 统计（末个 chunk 携带）
    bool is_end = false;                     // [DONE] 终止信号
};

} // namespace llm
