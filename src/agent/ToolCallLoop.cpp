// ToolCallLoop 实现 — Fix #1: 接受 IToolProvider&。

#include "agent/ToolCallLoop.h"
#include "agent/AgentState.h"
#include "agent/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

namespace {
// 将 LLMResponse 转为 assistant 消息并加入对话。
// 只复制/移动 response 中非空的字段，避免写入无意义的数据。
void addAssistantFromResponse(llm::Conversation& conversation, llm::LLMResponse& response) {
    llm::Message assistant;
    assistant.role = llm::MessageRole::Assistant;
    if (!response.content.empty())
        assistant.content = std::move(response.content);
    if (!response.reasoning_content.empty())
        assistant.reasoning_content = std::move(response.reasoning_content);
    if (!response.tool_calls.empty())
        assistant.tool_calls = response.tool_calls;  // 必须拷贝！调用方（execute/循环检测/取消路径）后续仍需读取 response.tool_calls
    conversation.add(std::move(assistant));
}
} // anonymous namespace

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
    ToolPipeline pipeline(tools_);                        // 工具执行管线：校验参数 → 执行 → 截断结果 → 生成 diff
    std::unordered_map<std::string, int> call_history;    // 调用历史：tool_name:args_json → 调用次数，用于重复检测

    ToolCallLoopResult r;
    llm::LLMResponse response;

    for (int round = 0; round < config.max_rounds; ++round) {
        // ── LLM 调用（首轮或后续）──
        int estimated = llm::TokenCounter::countMessages(conversation.messages());
        try {
            // ── 最后一轮：移除工具定义 + 提示 LLM 给出最终答复 ──
            if (round == config.max_rounds - 1) {
                std::string final_hint = system_prompt
                    + "\n\n注意：这是最后一轮对话，请直接给出最终答复，不要再调用工具。";
                response = client_.chat(conversation.messages(), {}, final_hint, callbacks);
            } else {
                response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
            }
            if (config.hooks.on_round_complete)
                config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens, estimated);
        } catch (const std::exception& e) {
            spdlog::error("[ToolCallLoop] 第 {} 轮 LLM 调用失败: {}", round, e.what());
            throw;
        }

        if (cancelled_ && *cancelled_) {
            llm::LLMResponse last_response = std::move(response);

            // 将本轮响应加入对话
            addAssistantFromResponse(conversation, last_response);

            // 有工具调用时加终止结果，后续 LLM 可见
            if (!last_response.tool_calls.empty()) {
                for (const auto& tc : last_response.tool_calls) {
                    nlohmann::json cancel_result = {
                        {"cancelled", true},
                        {"error", "任务已被用户取消"}
                    };
                    conversation.add(llm::Message::toolResult(tc.id, cancel_result.dump()));
                }
            }

            r.cancelled = true;
            last_response.tool_calls.clear();
            r.response = std::move(last_response);
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

        // 重复调用检测 → 通知 LLM 并获取最终答复
        if (has_repeated) {
            spdlog::warn("[ToolCallLoop] 检测到重复工具调用: {}，"
                         "将请求 LLM 直接给出最终答复", repeated_tool_name);

            // 保存当前响应
            llm::LLMResponse last_response = std::move(response);

            // 加入 assistant 消息（含重复的 tool_calls）到对话
            addAssistantFromResponse(conversation, last_response);

            // 为每个工具调用添加终止结果，确保消息序列合法
            for (const auto& tc : last_response.tool_calls) {
                nlohmann::json error_result = {
                    {"error", "工具调用已被系统终止：检测到重复调用"},
                    {"retryable", false}
                };
                conversation.add(llm::Message::toolResult(tc.id, error_result.dump()));
            }

            // 再发一轮：移除工具定义 + 提示重复调用
            std::string repeat_hint = system_prompt
                + "\n\n系统检测到工具调用（" + repeated_tool_name + "）出现重复，"
                + "已自动终止工具循环。请直接给出最终答复，不要再调用任何工具。";
            response = client_.chat(conversation.messages(), {}, repeat_hint, callbacks);

            if (config.hooks.on_round_complete)
                config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens,
                    llm::TokenCounter::countMessages(conversation.messages()));

            r.loop_detected = true;
            r.response = response;
            r.rounds_executed = round + 1;
            r.error = "检测到重复工具调用，已请求 LLM 给出最终答复";
            return r;
        }

        // ── 正常路径：追加 assistant + 执行工具 ──
        addAssistantFromResponse(conversation, response);

        if (state_) state_->transition(AgentState::AwaitingTool);

        try {
            auto diff = pipeline.execute(response.tool_calls);
            conversation.apply(diff);
        } catch (...) {
            conversation.popBack();
            throw;
        }

        if (state_) state_->transition(AgentState::Thinking);
    }

    // 达到最大轮数
    spdlog::warn("[ToolCallLoop] 达到最大轮数 ({})", config.max_rounds);
    r.response = response;
    r.rounds_executed = config.max_rounds;
    return r;
}

} // namespace agent
