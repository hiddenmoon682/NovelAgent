#pragma once

// Tool Call 循环引擎 — Fix #1: 依赖 IToolProvider& 替代 ToolRegistry&。
// Agent 和 SubAgent 均可使用（SubAgent 传入 RestrictedToolProvider）。

#include "agent/ExecutionTracer.h"
#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "llm/TokenCounter.h"

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

struct ToolCallLoopConfig {
    int max_rounds = 10;
    // 首轮 LLM 调用是否启用流式输出（用户可见实时打字效果）。
    // 默认 true：首轮流式，用户体验好。
    bool first_round_streaming = true;
    // 后续 tool_call 轮次是否也启用流式输出。
    // 默认 false：减少 SSE 连接开销。设为 true 可让用户在中间轮次也看到 LLM 输出，
    // 但会增加网络流量和 API 调用延迟。
    // 注意：即使设为 true，后续轮次也会通过 SSE 连接发送数据，
    // 流式连接与回调触发是不同机制；仅在首轮触发用户回调。
    bool all_rounds_streaming = false;
    std::chrono::seconds timeout{0};
    int max_repeated_calls = 3;
    int token_warning_threshold = 0;
    // CRIT-2: 最大反思轮数。检测到重复工具调用后，注入反思 prompt 让 LLM 自修正，
    // 达到此上限后仍未解决则终止（默认 3 轮）。0=不启用反思直接终止。
    int max_reflection_rounds = 3;
};

struct ToolCallLoopResult {
    llm::LLMResponse response;
    bool timed_out = false;
    bool cancelled = false;            //  Issue 21+26: 外部取消信号触发
    std::string error;
    int rounds_executed = 0;
    int total_tokens_used = 0;
    int input_tokens = 0;          //  累计 prompt_tokens（所有轮次），供 ContextManager::recordUsage 使用
    int output_tokens = 0;         //  累计 completion_tokens（所有轮次），供 ContextManager::recordUsage 使用
    bool loop_detected = false;
};

class StateMachine;

class ToolCallLoop {
public:
    // state 可选状态机指针（D1.1：工具执行前后触发状态转换，nullptr=不触发）
    ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                 ExecutionTracer* tracer = nullptr,
                 StateMachine* state = nullptr);

    // Issue 21+26: 设置外部取消标志（SubAgent 的超时取消信号）。
    // 在每轮循环开始处检查，被设置后立即终止并返回。
    void setCancelled(std::atomic<bool>* cancelled) { cancelled_ = cancelled; }

    // 执行 tool_call 循环。
    ToolCallLoopResult run(
        llm::Conversation& conversation,
        const std::vector<llm::ToolDefinition>& tools,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const ToolCallLoopConfig& config = {});

private:
    llm::ILLMClient& client_;
    IToolProvider& tools_;  // Fix #1: IToolProvider& 替代 ToolRegistry&
    ExecutionTracer* tracer_;
    StateMachine* state_;   // D1.1
    std::atomic<bool>* cancelled_ = nullptr;  // Issue 21+26: 外部取消信号

    // CRIT-2: 当前反思轮数计数器（累计此 loop.run 调用中的反思次数）。
    int reflection_rounds_ = 0;

    bool isRepeatedCall(const std::string& tool_name, const std::string& args_json,
                        std::unordered_map<std::string, int>& call_history,
                        int max_repeats) const;

    // CRIT-2: 构建反思 prompt。告知 LLM 它陷入了重复调用循环，要求尝试不同方法。
    static std::string buildReflectionPrompt(
        const std::string& tool_name, const std::string& args_preview, int round);
};

} // namespace agent
