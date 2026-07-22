#pragma once

// Tool Call 循环引擎 — Fix #1: 依赖 IToolProvider& 替代 ToolRegistry&。
// Agent 和 SubAgent 均可使用（SubAgent 传入 RestrictedToolProvider）。

#include "agent/IToolProvider.h"
#include "llm/IMemory.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "llm/TokenCounter.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

// 工具调用循环每轮完成后的回调集合（可选）。
// Agent 和 SubAgent 都通过 hook 获取 token 数据，不依赖 ToolCallLoopResult 中的统计字段。
struct ToolCallLoopHooks {
    // 每轮 LLM 调用完成后触发（含首轮、后续轮次、反思路径）。
    // input_tokens/output_tokens 为 API 返回的当前轮次实际值。
    // estimated_tokens 为 LLM 调用前对 conversation 的原始估算值。
    std::function<void(int input_tokens, int output_tokens, int estimated_tokens)> on_round_complete;
};

struct ToolCallLoopConfig {
    int max_rounds = 10;
    int max_repeated_calls = 3;
    ToolCallLoopHooks hooks;
    class ThreadPool* pool = nullptr;  // 可选线程池（并发执行只读工具）

    // ── 流式 setter（支持链式调用）──
    ToolCallLoopConfig& setMaxRounds(int n) { max_rounds = n; return *this; }
    ToolCallLoopConfig& setMaxRepeatedCalls(int n) { max_repeated_calls = (n > 0 ? n : 1); return *this; }
};

// ToolCallLoop::run() 的返回结果，包含 LLM 最终回复和终止原因。
// Token 统计不在此结构中传递——Agent 路径通过 on_round_complete hook 实时记录，
// SubAgent 路径通过自行设置的 hook 累加。
struct ToolCallLoopResult {
    // 最后一轮 LLM 的完整响应（含 content / tool_calls）。
    llm::LLMResponse response;

    // ── 终止原因（可共存）──

    bool cancelled = false;        //  外部取消
    bool loop_detected = false;    //  重复调用检测后终止

    // 终止描述，仅在 cancelled / loop_detected 时非空。
    std::string error;

    // ── 执行统计 ──

    int rounds_executed = 0;       //  实际执行了多少轮工具调用循环
};

class StateMachine;

class ToolCallLoop {
public:
    // state 可选状态机指针（D1.1：工具执行前后触发状态转换，nullptr=不触发）
    ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                 StateMachine* state = nullptr);

    // Issue 21+26: 设置外部取消标志（SubAgent 的超时取消信号 / 主 Agent 的 Ctrl+C）。
    // 在每轮循环开始处和 chat() 返回后检查，被设置后立即终止并返回。
    void setCancelled(std::atomic<bool>* cancelled) { cancelled_ = cancelled; }

    // 执行 tool_call 循环。
    ToolCallLoopResult run(
        llm::IMemory& memory,
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
