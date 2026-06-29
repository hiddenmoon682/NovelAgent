#pragma once

/// Tool Call 循环引擎 — Fix #1: 依赖 IToolProvider& 替代 ToolRegistry&。
/// Agent 和 SubAgent 均可使用（SubAgent 传入 RestrictedToolProvider）。

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
    /// 首轮 LLM 调用是否启用流式输出（用户可见实时打字效果）。
    /// 默认 true：首轮流式，用户体验好。
    bool first_round_streaming = true;
    /// 后续 tool_call 轮次是否也启用流式输出。
    /// 默认 false：减少 SSE 连接开销。设为 true 可让用户在中间轮次也看到 LLM 输出，
    /// 但会增加网络流量和 API 调用延迟（Issue 9 — 流式回调默认仅首轮生效）。
    bool all_rounds_streaming = false;
    std::chrono::seconds timeout{0};
    int max_repeated_calls = 3;
    int token_warning_threshold = 0;
};

struct ToolCallLoopResult {
    llm::LLMResponse response;
    bool timed_out = false;
    bool cancelled = false;            ///< Issue 21+26: 外部取消信号触发
    std::string error;
    int rounds_executed = 0;
    int total_tokens_used = 0;
    int input_tokens = 0;          ///< 累计 prompt_tokens（所有轮次），供 ContextManager::recordUsage 使用
    int output_tokens = 0;         ///< 累计 completion_tokens（所有轮次），供 ContextManager::recordUsage 使用
    bool loop_detected = false;
};

class StateMachine;

class ToolCallLoop {
public:
    /// @param state 可选状态机指针（D1.1：工具执行前后触发状态转换，nullptr=不触发）
    ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                 ExecutionTracer* tracer = nullptr,
                 StateMachine* state = nullptr);

    /// Issue 21+26: 设置外部取消标志（SubAgent 的超时取消信号）。
    /// 在每轮循环开始处检查，被设置后立即终止并返回。
    void setCancelled(std::atomic<bool>* cancelled) { cancelled_ = cancelled; }

    /// 执行 tool_call 循环。
    ///
    /// @param initial_messages 可选的外部消息列表（通常为 ContextManager 截断后的消息）。
    ///   首轮 LLM 调用优先使用此列表而非 conversation.messages()，
    ///   确保 token 截断策略真正生效（否则截断后的消息从未被使用）。
    ///   后续轮次（tool_call → tool_result 往返）仍使用 conversation.messages()
    ///   以携带完整的工具执行链。传 nullptr 或空 vector 退化为使用原始对话。
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
    StateMachine* state_;   // D1.1
    std::atomic<bool>* cancelled_ = nullptr;  // Issue 21+26: 外部取消信号

    bool isRepeatedCall(const std::string& tool_name, const std::string& args_json,
                        std::unordered_map<std::string, int>& call_history,
                        int max_repeats) const;
};

} // namespace agent
