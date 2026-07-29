#pragma once

// CoreLoop — 核心循环引擎，驱动 LLM ↔ 工具的多轮交互。
// Agent 和 SubAgent 均可使用（SubAgent 传入 RestrictedToolProvider）。

#include "agent/tool/IToolProvider.h"
#include "agent/context/IMemory.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "llm/TokenCounter.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

// 核心循环每轮完成后的回调集合（可选）。
// Agent 和 SubAgent 都通过 hook 获取 token 数据，不依赖 CoreLoopResult 中的统计字段。
struct CoreLoopHooks {
    // 每轮 LLM 调用完成后触发（含首轮、后续轮次、反思路径）。
    // input_tokens/output_tokens 为 API 返回的当前轮次实际值。
    // estimated_tokens 为 LLM 调用前对 conversation 的原始估算值。
    std::function<void(int input_tokens, int output_tokens, int estimated_tokens)> on_round_complete;

    // 工具结果回填 memory 之后、下一轮 LLM 调用之前触发。
    // WHY：on_round_complete 的触发点在 chat() 返回后、工具执行前，覆盖不了
    // 工具结果回填带来的 token 增量（单结果可达 32KB×N）——这是轮内累积
    // 溢出的盲区。调用方（Agent）在此评估预算并按需压缩；返回 false 表示
    // 压缩后仍超限，CoreLoop 将优雅终止循环而非等下一轮 API 返回 400。
    std::function<bool()> on_tool_results_applied;
};

struct CoreLoopConfig {
    int max_rounds = 10;
    int max_repeated_calls = 3;
    CoreLoopHooks hooks;

    // ── 流式 setter（支持链式调用）──
    CoreLoopConfig& setMaxRounds(int n) { max_rounds = n; return *this; }
    CoreLoopConfig& setMaxRepeatedCalls(int n) { max_repeated_calls = (n > 0 ? n : 1); return *this; }
};

// CoreLoop::run() 的返回结果，包含 LLM 最终回复和终止原因。
// Token 统计不在此结构中传递——Agent 路径通过 on_round_complete hook 实时记录，
// SubAgent 路径通过自行设置的 hook 累加。
struct CoreLoopResult {
    // 最后一轮 LLM 的完整响应（含 content / tool_calls）。
    llm::LLMResponse response;

    // ── 终止原因（可共存）──

    bool cancelled = false;        //  外部取消
    bool loop_detected = false;    //  重复调用检测后终止
    bool budget_exhausted = false; //  工具结果回填后上下文超限且压缩不足，提前终止

    // 终止描述，仅在 cancelled / loop_detected / budget_exhausted 时非空。
    std::string error;

    // ── 执行统计 ──

    int rounds_executed = 0;       //  实际执行了多少轮工具调用循环
};

class StateMachine;
class ToolPipeline;

class CoreLoop {
public:
    // pipeline  工具执行管线（由 Agent 持有，内含 ThreadPool，跨消息复用）
    // state     可选状态机指针（D1.1：工具执行前后触发状态转换，nullptr=不触发）
    CoreLoop(llm::ILLMClient& client, IToolProvider& tools,
             ToolPipeline& pipeline, StateMachine* state = nullptr);

    // Issue 21+26: 设置外部取消标志（SubAgent 的超时取消信号 / 主 Agent 的 Ctrl+C）。
    // 在每轮循环开始处和 chat() 返回后检查，被设置后立即终止并返回。
    void setCancelled(std::atomic<bool>* cancelled) { cancelled_ = cancelled; }

    // 执行 tool_call 循环（渐进式加载路径）。
    // 每轮从 tools_.getDefinitions() 动态获取当前工具列表，
    // 支持 tool_search 加载新工具后自动出现在后续轮次。
    CoreLoopResult run(
        llm::IMemory& memory,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const CoreLoopConfig& config = {});

    // 执行 tool_call 循环（固定工具列表路径，SubAgent 兼容）。
    CoreLoopResult run(
        llm::IMemory& memory,
        const std::vector<llm::ToolDefinition>& tools,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const CoreLoopConfig& config = {});

private:
    llm::ILLMClient& client_;
    IToolProvider& tools_;  // Fix #1: IToolProvider& 替代 ToolRegistry&
    ToolPipeline& pipeline_;
    StateMachine* state_;   // D1.1
    std::atomic<bool>* cancelled_ = nullptr;  // Issue 21+26: 外部取消信号

    // 核心循环实现。tools_override 非空时使用固定列表，否则每轮从 tools_ 动态获取。
    CoreLoopResult runImpl(
        llm::IMemory& memory,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const CoreLoopConfig& config,
        const std::vector<llm::ToolDefinition>* tools_override);

    bool isRepeatedCall(const std::string& tool_name, const std::string& args_json,
                        std::unordered_map<std::string, int>& call_history,
                        int max_repeats) const;
};

} // namespace agent
