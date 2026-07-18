// ToolCallLoop 实现 — Fix #1: 接受 IToolProvider&。

#include "agent/ToolCallLoop.h"
#include "agent/AgentState.h"
#include "agent/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

ToolCallLoop::ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                           StateMachine* state)
    : client_(client), tools_(tools), state_(state)
{}

bool ToolCallLoop::isRepeatedCall(
    const std::string& tool_name, const std::string& args_json,
    std::unordered_map<std::string, int>& call_history, int max_repeats) const
{
    std::string key = tool_name + ":" + args_json;
    if (++call_history[key] >= max_repeats) {
        spdlog::warn("[ToolCallLoop] 重复调用: {} ({}次)", tool_name, call_history[key]);
        return true;
    }
    return false;
}

ToolCallLoopResult ToolCallLoop::run(
    llm::Conversation& conversation,
    const std::vector<llm::ToolDefinition>& tools,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const ToolCallLoopConfig& config)
{
    ToolCallLoopResult result;                            // 最终结果（含 LLM 回复、token 统计、超时/取消标志）
    ToolPipeline pipeline(tools_, conversation);          // 工具执行管线：校验参数 → 执行 → 截断结果 → 生成 diff
    std::unordered_map<std::string, int> call_history;    // 调用历史：tool_name:args_json → 调用次数，用于重复检测

    auto executeLoop = [&]() -> ToolCallLoopResult {
        ToolCallLoopResult r;
        llm::LLMResponse response;

        for (int round = 0; round < config.max_rounds; ++round) {
            // ── LLM 调用（首轮或后续）──
            int estimated = llm::TokenCounter::countMessages(conversation.messages());
            response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
            r.total_tokens_used += response.total_tokens;
            r.input_tokens += response.prompt_tokens;
            r.output_tokens += response.completion_tokens;
            if (config.hooks.on_round_complete)
                config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens, estimated);

            if (config.token_warning_threshold > 0 &&
                r.total_tokens_used > config.token_warning_threshold) {
                spdlog::warn("[ToolCallLoop] Token 用量 {} 超过阈值 {}",
                             r.total_tokens_used, config.token_warning_threshold);
            }

            if (cancelled_ && *cancelled_) {
                r.cancelled = true;
                r.error = "任务已取消";
                return r;
            }

            if (response.tool_calls.empty()) {
                r.response = response;
                r.rounds_executed = round;
                return r;
            }

            spdlog::info("[ToolCallLoop] {} 个工具调用 (round={})",
                         response.tool_calls.size(), round);

            // ── 检测重复调用 ──
            bool has_repeated = false;
            std::string repeated_tool_name;
            std::string repeated_args;
            for (const auto& tc : response.tool_calls) {
                if (isRepeatedCall(tc.function_name, tc.arguments,
                                   call_history, config.max_repeated_calls)) {
                    has_repeated = true;
                    repeated_tool_name = tc.function_name;
                    repeated_args = tc.arguments;
                    break;
                }
            }

            // 重复调用检测 → 终止
            if (has_repeated) {
                r.loop_detected = true;
                r.error = "检测到重复工具调用循环，已自动终止";
                return r;
            }

            // ── 正常路径：追加 assistant + 执行工具 ──
            llm::Message assistant;
            assistant.role = llm::MessageRole::Assistant;
            assistant.content = std::move(response.content);
            assistant.reasoning_content = std::move(response.reasoning_content);
            assistant.tool_calls = response.tool_calls;
            conversation.add(std::move(assistant));

            if (state_) state_->transition(AgentState::AwaitingTool);

            auto diff = pipeline.execute(response.tool_calls);
            conversation.apply(diff);

            if (state_) state_->transition(AgentState::Thinking);
        }

        // 达到最大轮数
        spdlog::warn("[ToolCallLoop] 达到最大轮数 ({})", config.max_rounds);
        r.response = response;
        r.rounds_executed = config.max_rounds;
        return r;
    };

    if (config.timeout.count() > 0) {
        auto future = std::async(std::launch::async, executeLoop);
        if (future.wait_for(config.timeout) == std::future_status::timeout) {
            result.timed_out = true;
            result.error = "工具调用循环超时 (" + std::to_string(config.timeout.count()) + "s)";
            return result;
        }
        return future.get();
    }
    return executeLoop();
}

} // namespace agent
