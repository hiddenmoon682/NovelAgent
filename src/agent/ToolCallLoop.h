#pragma once

/// Tool Call 循环引擎 — Fix #1: 依赖 IToolProvider& 替代 ToolRegistry&。
/// Agent 和 SubAgent 均可使用（SubAgent 传入 RestrictedToolProvider）。

#include "agent/ExecutionTracer.h"
#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "llm/TokenCounter.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

struct ToolCallLoopConfig {
    int max_rounds = 10;
    bool first_round_streaming = true;
    bool all_rounds_streaming = false;
    std::chrono::seconds timeout{0};
    int max_repeated_calls = 3;
    int token_warning_threshold = 0;
};

struct ToolCallLoopResult {
    llm::LLMResponse response;
    bool timed_out = false;
    std::string error;
    int rounds_executed = 0;
    int total_tokens_used = 0;
    int input_tokens = 0;          ///< 累计 prompt_tokens（所有轮次）
    int output_tokens = 0;         ///< 累计 completion_tokens（所有轮次）
    bool loop_detected = false;
};

class ToolCallLoop {
public:
    ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                 ExecutionTracer* tracer = nullptr);

    ToolCallLoopResult run(
        llm::Conversation& conversation,
        const std::vector<llm::ToolDefinition>& tools,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const ToolCallLoopConfig& config = {},
        const std::vector<llm::Message>* initial_messages = nullptr);

private:
    llm::ILLMClient& client_;
    IToolProvider& tools_;  // Fix #1: IToolProvider& 替代 ToolRegistry&
    ExecutionTracer* tracer_;

    bool isRepeatedCall(const std::string& tool_name, const std::string& args_json,
                        std::unordered_map<std::string, int>& call_history,
                        int max_repeats) const;
};

} // namespace agent
