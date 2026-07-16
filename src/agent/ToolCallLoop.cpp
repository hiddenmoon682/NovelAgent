// ToolCallLoop 实现 — Fix #1: 接受 IToolProvider&。
// CRIT-2: 自修正反射机制（AutoGPT/LangGraph 反思模式）。

#include "agent/ToolCallLoop.h"
#include "agent/AgentState.h"
#include "agent/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

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

// CRIT-2: 构建反思 prompt，遵循 Open Agent 社区通用的 self-reflection 模式：
//   - AutoGPT: 工具执行错误后插入 Self-reflection 消息，列出已尝试的操作和结果
//   - LangGraph: Reflection node — (conversation, last_action) → 修正指导
//   - CrewAI: Self-Correction — 将错误上下文注入并递增 retry_count
// 本实现: 在 repeated-call 检测后注入 assistant 反思消息，让 LLM 自修正策略。
std::string ToolCallLoop::buildReflectionPrompt(
    const std::string& tool_name, const std::string& args_preview, int round)
{
    std::string prompt = "【反思 #" + std::to_string(round) + "】";
    prompt += "你反复调用了 " + tool_name + "(";
    if (args_preview.size() > 100) {
        prompt += args_preview.substr(0, 100) + "...";
    } else {
        prompt += args_preview;
    }
    prompt += ") 但未取得进展。这可能是因为：\n";
    prompt += "1. 你需要先读取其他章节或角色设定获取必要信息\n";
    prompt += "2. 当前工具的参数不正确或数据已存在\n";
    prompt += "3. 你应该换一个工具或换一种方式完成任务\n\n";
    prompt += "请分析当前情况，不要继续调用相同的工具，尝试一个不同的方法。\n";
    prompt += "如果你认为目标已完成，可以直接回复总结给用户。";
    return prompt;
}

ToolCallLoopResult ToolCallLoop::run(
    llm::Conversation& conversation,
    const std::vector<llm::ToolDefinition>& tools,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const ToolCallLoopConfig& config)
{
    ToolCallLoopResult result;                            // 最终结果（含 LLM 回复、token 统计、超时/取消标志）
    ToolPipeline pipeline(tools_, conversation);          // 工具执行管线：校验参数 → 执行 → 截断结果 → 生成 diff
    std::unordered_map<std::string, int> call_history;    // 调用历史：tool_name:args_json → 调用次数，用于重复检测

    // CRIT-2: 重置反思计数器
    reflection_rounds_ = 0;

    auto executeLoop = [&]() -> ToolCallLoopResult {
        ToolCallLoopResult r;

        // ── 首轮（带工具）──
        const auto& first_msgs = conversation.messages();
        llm::LLMResponse response;
        int estimated = llm::TokenCounter::countMessages(conversation.messages());
        response = client_.chat(first_msgs, tools, system_prompt, callbacks);
        r.total_tokens_used += response.total_tokens;
        r.input_tokens += response.prompt_tokens;
        r.output_tokens += response.completion_tokens;
        if (config.hooks.on_round_complete)
            config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens, estimated);

        if (config.token_warning_threshold > 0 &&
            r.total_tokens_used > config.token_warning_threshold) {
            spdlog::warn("[ToolCallLoop] Token 用量 {} 超过阈值 {}",
                         r.total_tokens_used, config.token_warning_threshold);
        }

        // ── 循环 ──
        for (int round = 0; round < config.max_rounds; ++round) {
            if (cancelled_ && *cancelled_) {
                r.cancelled = true;
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

            // CRIT-2: 反思路径 — 检测到重复且反思未耗尽
            if (has_repeated) {
                if (config.max_reflection_rounds > 0 &&
                    reflection_rounds_ < config.max_reflection_rounds) {
                    ++reflection_rounds_;
                    spdlog::warn("[ToolCallLoop] 反思 #{}/{}: {} 重复调用",
                                 reflection_rounds_, config.max_reflection_rounds,
                                 repeated_tool_name);

                    llm::Message reflection;
                    reflection.role = llm::MessageRole::Assistant;
                    reflection.content = buildReflectionPrompt(
                        repeated_tool_name, repeated_args, reflection_rounds_);
                    conversation.add(std::move(reflection));

                    // 跳过工具执行，直接调 LLM
                    int estimated = llm::TokenCounter::countMessages(conversation.messages());
                    response = client_.chat(
                        conversation.messages(), tools, system_prompt, callbacks);
                    r.total_tokens_used += response.total_tokens;
                    r.input_tokens += response.prompt_tokens;
                    r.output_tokens += response.completion_tokens;
                    if (config.hooks.on_round_complete)
                        config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens, estimated);
                    continue;
                }

                // 反思耗尽 → 终止
                r.loop_detected = true;
                r.error = "检测到重复工具调用循环，已自动终止（反思" +
                          std::to_string(reflection_rounds_) + "轮后未解决）";
                llm::Message err_msg;
                err_msg.role = llm::MessageRole::Tool;
                err_msg.content = "{\"error\":\"已经尝试了多种方法但陷入重复调用，请告知用户当前进度和遇到的问题。\"}";
                conversation.add(std::move(err_msg));
                return r;
            }

            // ── 正常路径：追加 assistant + 执行工具 + 调 LLM ──
            llm::Message assistant;
            assistant.role = llm::MessageRole::Assistant;
            assistant.content = std::move(response.content);
            assistant.reasoning_content = std::move(response.reasoning_content);
            assistant.tool_calls = response.tool_calls;  // 仍被后续 pipeline 使用
            conversation.add(std::move(assistant));

            if (state_) state_->transition(AgentState::AwaitingTool);

            auto diff = pipeline.execute(response.tool_calls);
            conversation.apply(diff);

            if (state_) state_->transition(AgentState::Thinking);

            // 后续 LLM 调用（流式，复用 callbacks 转发输出到用户）
            int estimated = llm::TokenCounter::countMessages(conversation.messages());
            response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
            r.total_tokens_used += response.total_tokens;
            r.input_tokens += response.prompt_tokens;
            r.output_tokens += response.completion_tokens;
        if (config.hooks.on_round_complete)
            config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens, estimated);

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
            result.error = "工具调用循环超时 (" + std::to_string(config.timeout.count()) + "s)";
            return result;
        }
        return future.get();
    }
    return executeLoop();
}

} // namespace agent
