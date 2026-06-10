#pragma once

/// Tool Call 循环引擎 — Agent 最佳实践增强版 (Fix #2,#3,#4,#9)。
///
/// Fix #2: 循环检测 — 追踪最近工具调用，重复3次以上→自动终止。
/// Fix #3: ExecutionTracer 集成 — 每个步骤自动记录轨迹。
/// Fix #4: Token 预算监控 — 调用前估算，调用后记录实际消耗。
/// Fix #9: 全轮次流式 — 首轮+后续轮次均流式，体验一致。

#include "agent/ExecutionTracer.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "llm/TokenCounter.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class ToolRegistry;

struct ToolCallLoopConfig {
    int max_rounds = 10;
    bool first_round_streaming = true;  // 首轮流式（用户看实时输出）
    bool all_rounds_streaming = false;  // Fix #9: 所有轮次流式（需要 Mock 支持）
    std::chrono::seconds timeout{0};
    int max_repeated_calls = 3;         // Fix #2: 重复调用上限
    int token_warning_threshold = 0;    // Fix #4: 0=不监控
};

struct ToolCallLoopResult {
    llm::LLMResponse response;
    bool timed_out = false;
    std::string error;
    int rounds_executed = 0;
    int total_tokens_used = 0;         // Fix #4
    bool loop_detected = false;        // Fix #2
};

class ToolCallLoop {
public:
    /// Fix #3: 构造函数接收 ExecutionTracer*（可选）。
    ToolCallLoop(llm::ILLMClient& client, ToolRegistry& registry,
                 ExecutionTracer* tracer = nullptr);

    ToolCallLoopResult run(
        llm::Conversation& conversation,
        const std::vector<llm::ToolDefinition>& tools,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const ToolCallLoopConfig& config = {});

    llm::ILLMClient& client() { return client_; }
    ToolRegistry& registry() { return registry_; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    ExecutionTracer* tracer_;  // Fix #3: 可空

    /// Fix #2: 检测重复调用。
    bool isRepeatedCall(const std::string& tool_name,
                        const std::string& args_json,
                        std::unordered_map<std::string, int>& call_history,
                        int max_repeats) const;
};

} // namespace agent
