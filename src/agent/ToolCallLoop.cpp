/// ToolCallLoop 实现。

#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

ToolCallLoop::ToolCallLoop(llm::ILLMClient& client, ToolRegistry& registry)
    : client_(client), registry_(registry)
{}

ToolCallLoopResult ToolCallLoop::run(
    llm::Conversation& conversation,
    const std::vector<llm::ToolDefinition>& tools,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const ToolCallLoopConfig& config)
{
    ToolCallLoopResult result;
    ToolPipeline pipeline(registry_, conversation);

    // ── 构造执行 lambda（供超时包装）──
    auto executeLoop = [&]() -> ToolCallLoopResult {
        ToolCallLoopResult r;

        // 首轮
        llm::LLMResponse response;
        if (config.first_round_streaming) {
            response = client_.chat(
                conversation.messages(), tools, system_prompt, std::move(callbacks));
        } else {
            response = client_.chatNonStreaming(
                conversation.messages(), tools, system_prompt);
        }

        // Tool call 循环
        for (int round = 0; round < config.max_rounds; ++round) {
            if (response.tool_calls.empty()) {
                r.response = response;
                r.rounds_executed = round;
                spdlog::debug("[ToolCallLoop] 完成 (round={})", round);
                return r;
            }

            spdlog::info("[ToolCallLoop] {} 个工具调用 (round={})",
                         response.tool_calls.size(), round);

            // 追加 assistant 消息
            llm::Message assistant;
            assistant.role = llm::MessageRole::Assistant;
            assistant.content = response.content;
            assistant.tool_calls = response.tool_calls;
            conversation.add(std::move(assistant));

            // 执行工具
            pipeline.executeAndAppend(response.tool_calls);

            // 后续轮次：非流式
            response = client_.chatNonStreaming(
                conversation.messages(), tools, system_prompt);
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
            result.error = "Tool call 循环超时 ("
                         + std::to_string(config.timeout.count()) + "s)";
            spdlog::warn("[ToolCallLoop] 超时");
            return result;
        }
        return future.get();
    }

    return executeLoop();
}

} // namespace agent
