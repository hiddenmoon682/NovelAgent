/// SubAgent 实现 — P0 重构版（IToolProvider + ToolCallLoop）。

#include "agent/SubAgent.h"
#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

SubAgent::SubAgent(llm::ILLMClient& client, IToolProvider& tools)
    : client_(client), tools_(tools)
{}

SubAgentResult SubAgent::execute(const SubAgentConfig& config)
{
    SubAgentResult result;

    auto tool_defs = tools_.getDefinitions();  // 已是受限列表，无需手动过滤
    conversation_.clear();

    spdlog::info("[SubAgent] 开始: {} (工具数={})",
                 config.task.substr(0, 60), tool_defs.size());

    // 使用 ToolCallLoop 引擎（与 Agent 共享实现）
    auto future = std::async(std::launch::async, [&]() -> SubAgentResult {
        SubAgentResult r;

        try {
            conversation_.addUser(config.task);

            ToolCallLoop loop(client_, *static_cast<ToolRegistry*>(nullptr));
            // 注：ToolCallLoop 需要 ToolRegistry& 用于 ToolPipeline，
            // 但这里 tools_ 是 IToolProvider&。我们需要让 ToolPipeline
            // 也接受 IToolProvider。
            //
            // 临时方案：使用 tools_ 提供的工具定义，手动执行循环
            // 此处保留为占位——Phase 5 完成 ToolPipeline 重构后将统一。

            // 直接调用 LLM（简化版，SubAgent 场景通常不需要复杂工具循环）
            llm::LLMResponse response = client_.chatNonStreaming(
                conversation_.messages(), tool_defs, config.system_prompt);

            for (int round = 0; round < config.max_tool_rounds; ++round) {
                if (response.tool_calls.empty()) {
                    r.output = response.content;
                    return r;
                }

                // 追加 assistant 消息
                llm::Message assistant;
                assistant.role = llm::MessageRole::Assistant;
                assistant.content = response.content;
                assistant.tool_calls = response.tool_calls;
                conversation_.add(std::move(assistant));

                // 执行工具（通过 IToolProvider 受限视图）
                for (const auto& tc : response.tool_calls) {
                    nlohmann::json args;
                    if (!tc.arguments.empty()) {
                        try { args = nlohmann::json::parse(tc.arguments); }
                        catch (...) { args = nlohmann::json::object(); }
                    }
                    auto result_json = tools_.execute(tc.function_name, args);
                    conversation_.addToolResult(tc.id, result_json.dump());
                }

                response = client_.chatNonStreaming(
                    conversation_.messages(), tool_defs, config.system_prompt);
            }

            r.output = response.content;
            spdlog::warn("[SubAgent] 达到最大轮数({})", config.max_tool_rounds);
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
