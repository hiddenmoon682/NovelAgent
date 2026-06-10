/// SubAgent 实现 — Fix #1: 完全委托 ToolCallLoop。

#include "agent/SubAgent.h"
#include "agent/ToolCallLoop.h"

#include <spdlog/spdlog.h>
#include <future>

namespace agent {

SubAgent::SubAgent(llm::ILLMClient& client, IToolProvider& tools)
    : client_(client), tools_(tools)
{}

SubAgentResult SubAgent::execute(const SubAgentConfig& config)
{
    SubAgentResult result;
    auto tool_defs = tools_.getDefinitions();
    conversation_.clear();

    spdlog::info("[SubAgent] 开始: {} (工具数={})",
                 config.task.substr(0, 60), tool_defs.size());

    auto future = std::async(std::launch::async, [&]() -> SubAgentResult {
        SubAgentResult r;
        try {
            conversation_.addUser(config.task);

            // Fix #1: 完全委托 ToolCallLoop（不再手写循环）
            ToolCallLoop loop(client_, tools_);
            ToolCallLoopConfig cfg;
            cfg.max_rounds = config.max_tool_rounds;
            cfg.first_round_streaming = false;  // SubAgent 无需流式输出
            cfg.max_repeated_calls = 3;

            auto loop_result = loop.run(
                conversation_, tool_defs, config.system_prompt, {}, cfg);
            r.output = loop_result.response.content;
            if (loop_result.timed_out) { r.timed_out = true; r.error = loop_result.error; }
            if (loop_result.loop_detected) r.error = loop_result.error;
        } catch (const std::exception& e) {
            r.error = e.what();
            spdlog::error("[SubAgent] 异常: {}", e.what());
        }
        return r;
    });

    auto status = future.wait_for(config.timeout);
    if (status == std::future_status::timeout) {
        result.timed_out = true;
        result.error = "子任务超时 (" + std::to_string(config.timeout.count()) + "s)";
        spdlog::warn("[SubAgent] 超时");
        return result;
    }
    return future.get();
}

} // namespace agent
