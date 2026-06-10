/// ToolCallLoop 实现 — Fix #1: 接受 IToolProvider&。

#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>
#include <chrono>

namespace agent {

ToolCallLoop::ToolCallLoop(llm::ILLMClient& client, IToolProvider& tools,
                           ExecutionTracer* tracer)
    : client_(client), tools_(tools), tracer_(tracer)
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
    ToolCallLoopResult result;
    ToolPipeline pipeline(tools_, conversation);  // Fix #1: tools_ 是 IToolProvider&
    std::unordered_map<std::string, int> call_history;

    if (tracer_) tracer_->record("llm_call", 0, 0);

    auto executeLoop = [&]() -> ToolCallLoopResult {
        ToolCallLoopResult r;

        // ── 首轮 ──
        auto t1 = std::chrono::steady_clock::now();
        llm::LLMResponse response;
        if (config.first_round_streaming || config.all_rounds_streaming)
            response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
        else
            response = client_.chatNonStreaming(conversation.messages(), tools, system_prompt);
        auto t2 = std::chrono::steady_clock::now();
        int round_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
        r.total_tokens_used += response.total_tokens;
        if (tracer_) tracer_->record("llm_response", response.total_tokens, round_ms);

        // Token 预算告警
        if (config.token_warning_threshold > 0 &&
            r.total_tokens_used > config.token_warning_threshold) {
            spdlog::warn("[ToolCallLoop] Token 用量 {} 超过阈值 {}",
                         r.total_tokens_used, config.token_warning_threshold);
        }

        // ── 循环 ──
        for (int round = 0; round < config.max_rounds; ++round) {
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

            // 执行工具
            auto t3 = std::chrono::steady_clock::now();
            pipeline.executeAndAppend(response.tool_calls);
            auto t4 = std::chrono::steady_clock::now();
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
        return r;
    };

    if (config.timeout.count() > 0) {
        auto future = std::async(std::launch::async, executeLoop);
        if (future.wait_for(config.timeout) == std::future_status::timeout) {
            result.timed_out = true;
            result.error = "Tool call 循环超时 (" + std::to_string(config.timeout.count()) + "s)";
            return result;
        }
        return future.get();
    }
    return executeLoop();
}

} // namespace agent
