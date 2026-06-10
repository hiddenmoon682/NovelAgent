/// ToolCallLoop 实现 — Agent 最佳实践增强版。

#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>
#include <chrono>

namespace agent {

ToolCallLoop::ToolCallLoop(llm::ILLMClient& client, ToolRegistry& registry,
                           ExecutionTracer* tracer)
    : client_(client), registry_(registry), tracer_(tracer)
{}

bool ToolCallLoop::isRepeatedCall(
    const std::string& tool_name, const std::string& args_json,
    std::unordered_map<std::string, int>& call_history, int max_repeats) const
{
    std::string key = tool_name + ":" + args_json;
    int count = ++call_history[key];
    if (count >= max_repeats) {
        spdlog::warn("[ToolCallLoop] 检测到重复调用: {} ({}次)", tool_name, count);
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
    ToolPipeline pipeline(registry_, conversation);
    std::unordered_map<std::string, int> call_history; // Fix #2

    // Fix #3: 记录用户输入
    if (tracer_) tracer_->record("llm_call", 0, 0);

    auto executeLoop = [&]() -> ToolCallLoopResult {
        ToolCallLoopResult r;

        // ── 首轮 ──
        auto t1 = std::chrono::steady_clock::now();
        llm::LLMResponse response;
        if (config.first_round_streaming || config.all_rounds_streaming) {
            response = client_.chat(
                conversation.messages(), tools, system_prompt, callbacks);
        } else {
            response = client_.chatNonStreaming(
                conversation.messages(), tools, system_prompt);
        }
        auto t2 = std::chrono::steady_clock::now();
        int round_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
        r.total_tokens_used += response.total_tokens;

        // Fix #3: 记录 LLM 调用
        if (tracer_) tracer_->record("llm_response", response.total_tokens, round_ms);

        // Fix #4: Token 预算告警
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

            // Fix #2: 检查每个工具调用是否重复
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
                // 构造一个 error response 返回给 LLM
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

            // Fix #3: 记录工具调用轨迹
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

            // Fix #9: 全轮次流式
            auto t5 = std::chrono::steady_clock::now();
            if (config.all_rounds_streaming) {
                response = client_.chat(
                    conversation.messages(), tools, system_prompt, {});
            } else {
                response = client_.chatNonStreaming(
                    conversation.messages(), tools, system_prompt);
            }
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

    // ── 超时控制 ──
    if (config.timeout.count() > 0) {
        auto future = std::async(std::launch::async, executeLoop);
        auto status = future.wait_for(config.timeout);
        if (status == std::future_status::timeout) {
            result.timed_out = true;
            result.error = "Tool call 循环超时 (" +
                std::to_string(config.timeout.count()) + "s)";
            if (tracer_) tracer_->record("error", 0, config.timeout.count() * 1000);
            return result;
        }
        return future.get();
    }

    return executeLoop();
}

} // namespace agent
