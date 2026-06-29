/// ToolCallLoop 实现 — Fix #1: 接受 IToolProvider&。

#include "agent/ToolCallLoop.h"
#include "agent/AgentState.h"
#include "agent/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>
#include <chrono>

namespace agent {

ToolCallLoop::ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                           ExecutionTracer* tracer, StateMachine* state)
    : client_(client), tools_(tools), tracer_(tracer), state_(state)
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
    const ToolCallLoopConfig& config,
    const std::vector<llm::Message>* initial_messages)
{
    ToolCallLoopResult result;
    ToolPipeline pipeline(tools_, conversation);  // Fix #1: tools_ 是 IToolProvider&
    std::unordered_map<std::string, int> call_history;

    if (tracer_) tracer_->record("llm_call", 0, 0);

    auto executeLoop = [&]() -> ToolCallLoopResult {
        ToolCallLoopResult r;

        // ── 首轮 ──
        // Fix: 如果提供了 initial_messages（截断后的消息），优先使用
        const auto& first_msgs = (initial_messages && !initial_messages->empty())
            ? *initial_messages : conversation.messages();
        auto t1 = std::chrono::steady_clock::now();
        llm::LLMResponse response;
        if (config.first_round_streaming || config.all_rounds_streaming)
            response = client_.chat(first_msgs, tools, system_prompt, callbacks);
        else
            response = client_.chatNonStreaming(first_msgs, tools, system_prompt);
        auto t2 = std::chrono::steady_clock::now();
        int round_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
        r.total_tokens_used += response.total_tokens;
        r.input_tokens += response.prompt_tokens;
        r.output_tokens += response.completion_tokens;
        if (tracer_) tracer_->record("llm_response", response.total_tokens, round_ms);

        // Token 预算告警
        if (config.token_warning_threshold > 0 &&
            r.total_tokens_used > config.token_warning_threshold) {
            spdlog::warn("[ToolCallLoop] Token 用量 {} 超过阈值 {}",
                         r.total_tokens_used, config.token_warning_threshold);
        }

        // ── 循环 ──
        for (int round = 0; round < config.max_rounds; ++round) {
            // Issue 21+26: 每轮检查外部取消信号（SubAgent 超时 → cancelled_=true）
            if (cancelled_ && *cancelled_) {
                r.cancelled = true;
                r.error = "任务已取消";
                if (tracer_) tracer_->record("error", r.total_tokens_used, 0,
                    {{"reason", "外部取消信号"}, {"round", round}});
                return r;
            }

            if (response.tool_calls.empty()) {
                r.response = response;
                r.rounds_executed = round;
                return r;
            }

            spdlog::info("[ToolCallLoop] {} 个工具调用 (round={})",
                         response.tool_calls.size(), round);

            // 检测重复调用
            bool has_repeated = false;
            for (const auto& tc : response.tool_calls) {
                if (isRepeatedCall(tc.function_name, tc.arguments,
                                   call_history, config.max_repeated_calls)) {
                    has_repeated = true;
                    break;
                }
            }
            if (has_repeated) {
                r.loop_detected = true;
                r.error = "检测到重复工具调用循环，已自动终止";
                if (tracer_) tracer_->record("error", r.total_tokens_used, 0,
                    {{"reason", "重复工具调用循环"}, {"round", round}});
                // 向对话注入错误上下文，让调用方了解终止原因
                llm::Message err_msg;
                err_msg.role = llm::MessageRole::Tool;
                err_msg.content = "{\"error\":\"重复工具调用循环已终止，请尝试其他方式完成任务。\"}";
                conversation.add(std::move(err_msg));
                return r;
            }

            // 追加 assistant 消息
            llm::Message assistant;
            assistant.role = llm::MessageRole::Assistant;
            assistant.content = response.content;
            assistant.tool_calls = response.tool_calls;
            conversation.add(std::move(assistant));

            // 记录工具调用轨迹（包含参数）
            if (tracer_) {
                for (const auto& tc : response.tool_calls)
                    tracer_->record("tool_call", 0, 0,
                        {{"name", tc.function_name}, {"args", tc.arguments}});
            }

            // D1.1: 工具执行前 → AwaitingTool
            if (state_) state_->transition(AgentState::AwaitingTool);

            // 执行工具 — Issue 2: 使用 execute() 返回 diff，集中 apply
            auto t3 = std::chrono::steady_clock::now();
            auto diff = pipeline.execute(response.tool_calls);
            conversation.apply(diff);
            auto t4 = std::chrono::steady_clock::now();

            // D1.1: 工具执行后 → Thinking
            if (state_) state_->transition(AgentState::Thinking);
            int tool_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count());

            if (tracer_) tracer_->record("tool_result", 0, tool_ms);

            // 后续 LLM 调用
            auto t5 = std::chrono::steady_clock::now();
            if (config.all_rounds_streaming)
                response = client_.chat(conversation.messages(), tools, system_prompt, {});
            else
                response = client_.chatNonStreaming(conversation.messages(), tools, system_prompt);
            auto t6 = std::chrono::steady_clock::now();
            round_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t6 - t5).count());
            r.total_tokens_used += response.total_tokens;
            r.input_tokens += response.prompt_tokens;
            r.output_tokens += response.completion_tokens;

            if (tracer_) tracer_->record("llm_response", response.total_tokens, round_ms);

            if (config.token_warning_threshold > 0 &&
                r.total_tokens_used > config.token_warning_threshold) {
                spdlog::warn("[ToolCallLoop] Token 用量 {} 超过阈值",
                             r.total_tokens_used);
            }
        }

        r.response = response;
        r.rounds_executed = config.max_rounds;
        spdlog::warn("[ToolCallLoop] 达到最大轮数 ({})", config.max_rounds);
        if (tracer_) tracer_->record("error", r.total_tokens_used, 0,
            {{"reason", "达到最大工具调用轮数"}, {"max_rounds", config.max_rounds}});
        return r;
    };

    if (config.timeout.count() > 0) {
        auto future = std::async(std::launch::async, executeLoop);
        if (future.wait_for(config.timeout) == std::future_status::timeout) {
            result.timed_out = true;
            result.error = "工具调用循环超时 (" + std::to_string(config.timeout.count()) + "s)";
            if (tracer_) tracer_->record("error", 0, static_cast<int>(config.timeout.count()) * 1000,
                {{"reason", "工具调用循环超时"}, {"timeout_s", config.timeout.count()}});
            return result;
        }
        return future.get();
    }
    return executeLoop();
}

} // namespace agent
