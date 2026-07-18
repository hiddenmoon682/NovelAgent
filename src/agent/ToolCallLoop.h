#pragma once

// Tool Call 循环引擎 — Fix #1: 依赖 IToolProvider& 替代 ToolRegistry&。
// Agent 和 SubAgent 均可使用（SubAgent 传入 RestrictedToolProvider）。

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

// 工具调用循环每轮完成后的回调集合（可选）。
// SubAgent 不传 hooks，不影响轻量路径。
struct ToolCallLoopHooks {
    // 每轮 LLM 调用完成后触发（含首轮、后续轮次、反思路径）。
    // input_tokens/output_tokens 为 API 返回的当前轮次实际值。
    // estimated_tokens 为 LLM 调用前对 conversation 的原始估算值。
    std::function<void(int input_tokens, int output_tokens, int estimated_tokens)> on_round_complete;
};

struct ToolCallLoopConfig {
    int max_rounds = 10;
    // 是否启用流式输出（未使用——run() 中始终流式）。
    bool streaming = true;
    std::chrono::seconds timeout{0};
    int max_repeated_calls = 3;
    int token_warning_threshold = 0;
    // 可选回调，用于每轮完成后的 token 跟踪和上下文管理。
    ToolCallLoopHooks hooks;

    // ── 流式 setter（支持链式调用）──
    ToolCallLoopConfig& setMaxRounds(int n) { max_rounds = n; return *this; }
    ToolCallLoopConfig& setStreaming(bool v) { streaming = v; return *this; }
    ToolCallLoopConfig& setTimeout(std::chrono::seconds t) { timeout = t; return *this; }
    ToolCallLoopConfig& setMaxRepeatedCalls(int n) { max_repeated_calls = n; return *this; }
    ToolCallLoopConfig& setTokenWarningThreshold(int t) { token_warning_threshold = t; return *this; }
};

// ToolCallLoop::run() 的返回结果，包含 LLM 最终回复、终止原因及 token 统计。
// 调用方（Agent / SubAgent）通过此结构判断循环是否正常结束、因何终止。
struct ToolCallLoopResult {
    // 最后一轮 LLM 的完整响应（含 content / tool_calls）。
    // 正常结束时包含最终回复内容；反思终止时此字段可能为空。
    llm::LLMResponse response;

    // ── 终止原因（三选一或互斥）──

    bool timed_out = false;        //  超时终止 — 总执行时间超过 config.timeout
    bool cancelled = false;        //  外部取消 — Issue 21+26: SubAgent 主动取消信号触发
    bool loop_detected = false;    //  循环终止 — 反思轮数耗尽，重复工具调用仍未解决

    // 人类可读的错误/终止描述。仅在 timed_out / cancelled / loop_detected 时非空。
    // 例如："工具调用循环超时 (60s)" 或 "检测到重复工具调用循环，已自动终止（反思3轮后未解决）"
    std::string error;

    // ── 执行统计 ──

    int rounds_executed = 0;       //  实际执行的 LLM 调用轮数（含首轮和反思路径的 LLM 调用）
    int total_tokens_used = 0;     //  所有轮次累计 token 消耗（total = prompt + completion）
    int input_tokens = 0;          //  累计 prompt_tokens（所有轮次），供 ContextManager::recordUsage 使用
    int output_tokens = 0;         //  累计 completion_tokens（所有轮次），供 ContextManager::recordUsage 使用
};

class StateMachine;

class ToolCallLoop {
public:
    // state 可选状态机指针（D1.1：工具执行前后触发状态转换，nullptr=不触发）
    ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
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
    StateMachine* state_;   // D1.1
    std::atomic<bool>* cancelled_ = nullptr;  // Issue 21+26: 外部取消信号

    bool isRepeatedCall(const std::string& tool_name, const std::string& args_json,
                        std::unordered_map<std::string, int>& call_history,
                        int max_repeats) const;
};

} // namespace agent
