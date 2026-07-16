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
    // CRIT-2: 最大反思轮数。检测到重复工具调用后，注入反思 prompt 让 LLM 自修正，
    // 达到此上限后仍未解决则终止（默认 3 轮）。0=不启用反思直接终止。
    int max_reflection_rounds = 3;
    // 可选回调，用于每轮完成后的 token 跟踪和上下文管理。
    ToolCallLoopHooks hooks;

    // ── 流式 setter（支持链式调用）──
    ToolCallLoopConfig& setMaxRounds(int n) { max_rounds = n; return *this; }
    ToolCallLoopConfig& setStreaming(bool v) { streaming = v; return *this; }
    ToolCallLoopConfig& setTimeout(std::chrono::seconds t) { timeout = t; return *this; }
    ToolCallLoopConfig& setMaxRepeatedCalls(int n) { max_repeated_calls = n; return *this; }
    ToolCallLoopConfig& setTokenWarningThreshold(int t) { token_warning_threshold = t; return *this; }
    ToolCallLoopConfig& setMaxReflectionRounds(int n) { max_reflection_rounds = n; return *this; }
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
