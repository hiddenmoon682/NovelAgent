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
        auto t1 = std::chrono::steady_clock::now();
        llm::LLMResponse response;
        if (config.first_round_streaming || config.all_rounds_streaming)
            response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
        else
            response = client_.chatNonStreaming(conversation.messages(), tools, system_prompt);
        auto t2 = std::chrono::steady_clock::now();
        r.total_tokens_used += response.total_tokens;
        if (tracer_) tracer_->record("llm_response", response.total_tokens,
            static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()));

        for (int round = 0; round < config.max_rounds; ++round) {
            if (response.tool_calls.empty()) { r.response = response; r.rounds_executed = round; return r; }

            spdlog::info("[ToolCallLoop] {} 个工具调用 (round={})", response.tool_calls.size(), round);
            bool has_repeated = false;
            for (const auto& tc : response.tool_calls)
                if (isRepeatedCall(tc.function_name, tc.arguments, call_history, config.max_repeated_calls))
                    { has_repeated = true; break; }
            if (has_repeated) {
                r.loop_detected = true;
                r.error = "检测到重复工具调用循环，已自动终止";
                return r;
            }

            llm::Message assistant;
            assistant.role = llm::MessageRole::Assistant;
            assistant.content = response.content;
            assistant.tool_calls = response.tool_calls;
            conversation.add(std::move(assistant));

            if (tracer_) for (const auto& tc : response.tool_calls)
                tracer_->record("tool_call", 0, 0, {{"name", tc.function_name}});

            pipeline.executeAndAppend(response.tool_calls);

            if (tracer_) tracer_->record("tool_result", 0, 0);

            if (config.all_rounds_streaming)
                response = client_.chat(conversation.messages(), tools, system_prompt, {});
            else
                response = client_.chatNonStreaming(conversation.messages(), tools, system_prompt);
            r.total_tokens_used += response.total_tokens;
            if (tracer_) tracer_->record("llm_response", response.total_tokens, 0);
        }
        r.response = response; r.rounds_executed = config.max_rounds;
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
