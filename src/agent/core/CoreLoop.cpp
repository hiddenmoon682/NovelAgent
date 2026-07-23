// CoreLoop 实现 — 核心循环引擎，接受 IToolProvider&。

#include "agent/core/CoreLoop.h"
#include "agent/core/AgentState.h"
#include "agent/tool/ToolPipeline.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

namespace {
// 将 LLMResponse 转为 assistant 消息并加入记忆。
// 只复制/移动 response 中非空的字段，避免写入无意义的数据。
void addAssistantFromResponse(llm::IMemory& memory, llm::LLMResponse& response) {
    llm::Message assistant;
    assistant.role = llm::MessageRole::Assistant;
    if (!response.content.empty())
        assistant.content = std::move(response.content);
    if (!response.reasoning_content.empty())
        assistant.reasoning_content = std::move(response.reasoning_content);
    if (!response.tool_calls.empty())
        assistant.tool_calls = response.tool_calls;  // 必须拷贝！调用方（execute/循环检测/取消路径）后续仍需读取 response.tool_calls
    memory.inject(std::move(assistant));
}

// 构造取消提示 assistant 消息，用于取消时维持一问一答对话格式。
llm::Message cancelledAssistant() {
    llm::Message msg;
    msg.role = llm::MessageRole::Assistant;
    msg.content = "对话已被用户取消，请等待下一条用户输入。";
    return msg;
}
} // anonymous namespace

CoreLoop::CoreLoop(llm::ILLMClient& client, IToolProvider& tools,
                   ToolPipeline& pipeline, StateMachine* state)
    : client_(client), tools_(tools), pipeline_(pipeline), state_(state)
{}

bool CoreLoop::isRepeatedCall(
    const std::string& tool_name, const std::string& args_json,
    std::unordered_map<std::string, int>& call_history, int max_repeats) const
{
    // 归一化 JSON：解析后重序列化，消除键顺序差异
    // nlohmann::json::dump() 以 std::map 迭代输出，键为字典序
    std::string normalized;
    try {
        normalized = nlohmann::json::parse(args_json).dump();
    } catch (...) {
        normalized = args_json;  // 非法 JSON 回退原始字符串
    }
    std::string key = tool_name + ":" + normalized;
    if (++call_history[key] >= max_repeats) {
        spdlog::warn("[CoreLoop] 重复调用: {} ({}次)", tool_name, call_history[key]);
        return true;
    }
    return false;
}

CoreLoopResult CoreLoop::run(
    llm::IMemory& memory,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const CoreLoopConfig& config)
{
    return runImpl(memory, system_prompt, std::move(callbacks), config, nullptr);
}

CoreLoopResult CoreLoop::run(
    llm::IMemory& memory,
    const std::vector<llm::ToolDefinition>& tools,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const CoreLoopConfig& config)
{
    return runImpl(memory, system_prompt, std::move(callbacks), config, &tools);
}

CoreLoopResult CoreLoop::runImpl(
    llm::IMemory& memory,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const CoreLoopConfig& config,
    const std::vector<llm::ToolDefinition>* tools_override)
{
    std::unordered_map<std::string, int> call_history;    // 调用历史：tool_name:args_json → 调用次数，用于重复检测

    CoreLoopResult r;
    llm::LLMResponse response;

    for (int round = 0; round < config.max_rounds; ++round) {
        // ── [#9 修复] 首轮开始前检查取消，不浪费 LLM API 调用 ──
        // 后续轮次上一轮已执行工具，对话中有 tool_result，不能直接退出。
        if (round == 0 && cancelled_ && *cancelled_) {
            spdlog::warn("[CoreLoop] 首轮前取消信号，退出 (round=0)");
            // 加空 assistant 消息维持一问一答格式
            memory.inject(cancelledAssistant());
            r.cancelled = true;
            r.rounds_executed = 0;       // [#11 修复] 记录实际轮数
            r.error = "任务已取消";
            return r;
        }

        // ── 动态获取当前轮次的工具列表 ──
        const auto tools = tools_override ? *tools_override : tools_.getDefinitions();

        // ── LLM 调用（首轮或后续）──
        int estimated = llm::TokenCounter::countMessages(memory.messages())
            + llm::TokenCounter::countTokens(system_prompt);  // 包含 system prompt，确保与 API 的 prompt_tokens 口径一致
        try {
            // ── 如果是最后一轮：移除工具定义 + 提示 LLM 给出最终答复 ──
            if (round == config.max_rounds - 1) {
                std::string final_hint = system_prompt
                    + "\n\n注意：这是最后一轮对话，请直接给出最终答复，不要再调用工具。";
                response = client_.chat(memory.messages(), {}, final_hint, callbacks, cancelled_);
            } else {
                response = client_.chat(memory.messages(), tools, system_prompt, callbacks, cancelled_);
            }
            if (config.hooks.on_round_complete)
                config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens, estimated);
        } catch (const std::exception& e) {
            spdlog::error("[CoreLoop] 第 {} 轮 LLM 调用失败: {}", round, e.what());
            throw;
        }

        // ── chat() 期间取消检查（SSE 回调中止后返回部分响应）──
        if (cancelled_ && *cancelled_) {
            spdlog::warn("[CoreLoop] chat() 期间取消，退出 (round={})", round);
            // 加空 assistant 消息维持一问一答格式，避免上一轮 tool_result 孤立
            memory.inject(cancelledAssistant());
            r.cancelled = true;
            r.rounds_executed = round + 1;
            r.error = "任务已取消";
            return r;
        }

        if (response.tool_calls.empty()) {
            r.response = response;
            r.rounds_executed = round + 1;
            return r;
        }

        spdlog::info("[CoreLoop] {} 个工具调用 (round={})",
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
            spdlog::warn("[CoreLoop] 检测到重复工具调用: {}，"
                         "将请求 LLM 直接给出最终答复", repeated_tool_name);

            // 保存当前响应
            llm::LLMResponse last_response = std::move(response);

            // 加入 assistant 消息（含重复的 tool_calls）到记忆
            addAssistantFromResponse(memory, last_response);

            // 为每个工具调用添加终止结果，确保消息序列合法
            for (const auto& tc : last_response.tool_calls) {
                nlohmann::json error_result = {
                    {"error", "工具调用已被系统终止：检测到重复调用"},
                    {"retryable", false}
                };
                memory.inject(llm::Message::toolResult(tc.id, error_result.dump()));
            }

            // 再发一轮：移除工具定义 + 提示重复调用
            std::string repeat_hint = system_prompt
                + "\n\n系统检测到工具调用（" + repeated_tool_name + "）出现重复，"
                + "已自动终止工具循环。请直接给出最终答复，不要再调用任何工具。";
            response = client_.chat(memory.messages(), {}, repeat_hint, callbacks, cancelled_);

            if (config.hooks.on_round_complete)
                config.hooks.on_round_complete(response.prompt_tokens, response.completion_tokens,
                    llm::TokenCounter::countMessages(memory.messages()));

            r.loop_detected = true;
            r.response = response;
            r.rounds_executed = round + 1;
            r.error = "检测到重复工具调用，已请求 LLM 给出最终答复";
            return r;
        }

        // ── 正常路径：追加 assistant + 执行工具 ──
        addAssistantFromResponse(memory, response);

        if (state_) state_->transition(AgentState::AwaitingTool);

        try {
            auto diff = pipeline_.execute(response.tool_calls);
            memory.apply(diff);
        } catch (...) {
            memory.popBack();
            throw;
        }

        if (state_) state_->transition(AgentState::Thinking);
    }

    // 达到最大轮数
    spdlog::warn("[CoreLoop] 达到最大轮数 ({})", config.max_rounds);
    r.response = response;
    r.rounds_executed = config.max_rounds;
    return r;
}

} // namespace agent
