#include "agent/Agent.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace agent {

// ===========================================================================
// 构造 / 配置
// ===========================================================================

Agent::Agent(llm::LLMClient& client, ToolRegistry& registry)
    : client_(client)
    , registry_(registry)
{
}

void Agent::setSystemPrompt(std::string prompt)
{
    system_prompt_ = std::move(prompt);
}

void Agent::setMaxToolRounds(int n)
{
    max_tool_rounds_ = (n >= 1) ? n : 1;
}

void Agent::clearConversation()
{
    conversation_.clear();
}

// ===========================================================================
// processUserMessage — 用户消息入口
// ===========================================================================

llm::LLMResponse Agent::processUserMessage(const std::string& input,
                                            llm::StreamCallbacks callbacks)
{
    if (input.empty()) {
        spdlog::warn("[Agent] 收到空输入");
        return llm::LLMResponse{};
    }

    // 1. 将用户消息加入对话历史
    conversation_.addUser(input);

    // 2. 调用 LLM + tool call 循环
    llm::LLMResponse response;
    try {
        response = runToolLoop(std::move(callbacks));
    } catch (...) {
        // 异常时保留对话历史（已加入的用户消息），向上抛出
        throw;
    }

    // 3. 将最终 assistant 回复加入对话历史
    if (!response.content.empty() || !response.tool_calls.empty()) {
        conversation_.add(makeAssistantMessage(response));
    }

    return response;
}

// ===========================================================================
// execute — 单次命令模式
// ===========================================================================

llm::LLMResponse Agent::execute(const std::string& command,
                                 llm::StreamCallbacks callbacks)
{
    // 不修改内部 conversation_，构造临时消息列表
    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();

    return client_.chat(messages, tools, system_prompt_, std::move(callbacks));
}

// ===========================================================================
// runToolLoop — tool call 循环
//
// 首轮使用流式调用（用户实时看到输出），后续轮次使用非流式调用
//（工具执行期间不需要实时显示，且非流式更简单、更快）。
// ===========================================================================

llm::LLMResponse Agent::runToolLoop(llm::StreamCallbacks callbacks)
{
    auto tools = registry_.getToolDefinitions();

    // 首轮：流式调用（用户看到实时 token 输出）
    auto response = client_.chat(
        conversation_.messages(),
        tools,
        system_prompt_,
        std::move(callbacks)
    );

    // 后续轮次：非流式调用 + 工具执行循环
    for (int round = 0; round < max_tool_rounds_; ++round) {
        if (response.tool_calls.empty()) {
            spdlog::debug("[Agent] LLM 返回纯文本（无 tool_calls），循环结束 (round={})", round);
            return response;
        }

        spdlog::info("[Agent] LLM 请求 {} 个工具调用 (round={})",
                     response.tool_calls.size(), round);

        // 将 assistant 消息（含 tool_calls）加入对话
        conversation_.add(makeAssistantMessage(response));

        // 执行工具并将结果加入对话
        executeToolCallsAndAppend(response.tool_calls);

        // 工具结果后的 LLM 调用使用非流式
        response = client_.chatNonStreaming(
            conversation_.messages(),
            tools,
            system_prompt_
        );
    }

    // 超过最大轮数 → 返回最后一次响应（即使仍有 tool_calls）
    spdlog::warn("[Agent] 达到最大 tool call 轮数 ({})，强制退出", max_tool_rounds_);
    return response;
}

// ===========================================================================
// 辅助方法
// ===========================================================================

llm::Message Agent::makeAssistantMessage(const llm::LLMResponse& response)
{
    llm::Message msg;
    msg.role = llm::MessageRole::Assistant;
    msg.content = response.content;
    msg.tool_calls = response.tool_calls;
    return msg;
}

void Agent::executeToolCallsAndAppend(
    const std::vector<llm::ToolCall>& tool_calls)
{
    for (const auto& tc : tool_calls) {
        spdlog::info("[Agent] 执行工具: {} (id={})", tc.function_name, tc.id);

        nlohmann::json args;
        if (!tc.arguments.empty()) {
            try {
                args = nlohmann::json::parse(tc.arguments);
            } catch (const nlohmann::json::parse_error& e) {
                spdlog::error("[Agent] 工具参数 JSON 解析失败: {} — args='{}'",
                              e.what(), tc.arguments);
                conversation_.addToolResult(tc.id,
                    R"({"error": "参数 JSON 解析失败: )" + std::string(e.what()) + R"("})");
                continue;
            }
        }

        auto result = registry_.executeTool(tc.function_name, args);
        conversation_.addToolResult(tc.id, result.dump());
    }
}

} // namespace agent
