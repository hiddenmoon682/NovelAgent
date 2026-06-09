#include "agent/SubAgent.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>
#include <future>
#include <algorithm>

namespace agent {

SubAgent::SubAgent(llm::ILLMClient& client, ToolRegistry& registry)
    : client_(client), registry_(registry)
{
}

std::vector<llm::ToolDefinition> SubAgent::filterTools(
    const std::vector<std::string>& allowed) const
{
    if (allowed.empty()) return {};

    auto allDefs = registry_.getToolDefinitions();
    std::vector<llm::ToolDefinition> filtered;
    for (const auto& def : allDefs) {
        if (std::find(allowed.begin(), allowed.end(), def.name) != allowed.end()) {
            filtered.push_back(def);
        }
    }
    return filtered;
}

SubAgentResult SubAgent::execute(const SubAgentConfig& config)
{
    SubAgentResult result;

    auto tools = filterTools(config.allowed_tools);

    // 构造子 Agent 专用消息
    conversation_.clear();
    conversation_.addSystem(config.system_prompt);
    conversation_.addUser(config.task);

    spdlog::info("[SubAgent] 开始执行: {} (工具数={})",
                 config.task.substr(0, 60), tools.size());

    // 使用 std::async 支持超时
    auto future = std::async(std::launch::async, [&]() -> SubAgentResult {
        SubAgentResult r;

        try {
            ToolPipeline pipeline(registry_, conversation_);

            // 首轮：非流式
            auto response = client_.chatNonStreaming(
                conversation_.messages(), tools, config.system_prompt);

            // 工具调用循环（复用 ToolPipeline）
            for (int round = 0; round < config.max_tool_rounds; ++round) {
                if (response.tool_calls.empty()) {
                    r.output = response.content;
                    spdlog::debug("[SubAgent] 完成 (round={})", round);
                    return r;
                }

                // 追加 assistant 消息 + 通过 ToolPipeline 执行工具
                llm::Message assistant;
                assistant.role = llm::MessageRole::Assistant;
                assistant.content = response.content;
                assistant.tool_calls = response.tool_calls;
                conversation_.add(assistant);
                pipeline.executeAndAppend(response.tool_calls);

                response = client_.chatNonStreaming(
                    conversation_.messages(), tools, config.system_prompt);
            }

            r.output = response.content;
            spdlog::warn("[SubAgent] 达到最大轮数({})", config.max_tool_rounds);
        } catch (const std::exception& e) {
            r.error = e.what();
            spdlog::error("[SubAgent] 异常: {}", e.what());
        }

        return r;
    });

    // 等待结果或超时
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
