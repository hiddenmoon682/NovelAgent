/// Agent 实现 — Agent 最佳实践增强版 (Fix #3,#6) + Phase 4 线程安全 (LLMClientFactory)。

#include "agent/Agent.h"
#include "agent/AgentOrchestrator.h"
#include "agent/ContextManager.h"
#include "agent/PromptComposer.h"
#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"
#include "llm/LLMClientFactory.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace agent {

Agent::Agent(llm::LLMClientFactory& factory, ToolRegistry& registry)
    : factory_(factory), client_(factory.create()), registry_(registry)
{
    useSerialProcessor();
}

Agent::~Agent() = default;

void Agent::setSystemPrompt(std::string prompt) {
    system_prompt_ = std::move(prompt);
    if (processor_) processor_->setSystemPrompt(system_prompt_);  // Fix #3
}
void Agent::setMaxToolRounds(int n) { max_tool_rounds_ = (n >= 1) ? n : 1; }
void Agent::setContextManager(ContextManager* cm) { context_manager_ = cm; }
void Agent::setContextWindow(int window) { context_window_ = window; }
void Agent::clearConversation() { conversation_.clear(); }

void Agent::useSerialProcessor() {
    auto sp = std::make_unique<SerialProcessor>(*client_, registry_, system_prompt_);
    sp->setContextManager(context_manager_);
    sp->setContextWindow(context_window_);
    sp->setMaxToolRounds(max_tool_rounds_);
    // Fix #3: 传递 tracer 给 SerialProcessor
    sp->setTracer(&tracer_);
    processor_ = std::move(sp);
}

void Agent::useParallelProcessor(TemplateManager* tm) {
    auto pp = std::make_unique<ParallelProcessor>(factory_, registry_, system_prompt_);
    if (tm) pp->setTemplateManager(tm);
    processor_ = std::move(pp);
    spdlog::info("[Agent] 切换到并行处理器");
}

void Agent::setProcessor(std::unique_ptr<IMessageProcessor> p) { processor_ = std::move(p); }
bool Agent::isParallelEnabled() const {
    return dynamic_cast<ParallelProcessor*>(processor_.get()) != nullptr;
}

// ============================================================================
// processUserMessage — Agent 核心入口
//
// 职责：接收用户自然语言输入，驱动 LLM 多轮 tool_call 循环，返回最终回复。
//
// 完整流程:
//   1. 输入守卫   — 拒绝空输入（fail-fast）
//   2. 状态守卫   — 检查 StateMachine 是否可接受输入；尝试从 Error 自动恢复
//   3. 轨迹记录   — ExecutionTracer 记录用户输入（Fix #3）
//   4. 状态转换   — Idle → Thinking（Fix #6）
//   5. 核心处理   — 委托 IMessageProcessor::process() 执行 LLM 多轮调用
//   6. 状态恢复   — Thinking → Idle
//   7. 轨迹记录   — 记录完成信息（token 消耗等）
//   8. 返回结果   — LLMResponse（含文本、token 计数、tool_call 详情）
//
// 注意：
//   - conversation_ 以引用传入 processor_，processor_ 内部会自动追加对话历史
//   - 此方法不抛异常，错误通过空 LLMResponse 或日志方式上报
// ============================================================================

llm::LLMResponse Agent::processUserMessage(const std::string& input,
                                            llm::StreamCallbacks callbacks)
{
    // ── 步骤 1: 输入守卫 ──
    // 拒绝空字符串输入，避免无效请求进入 LLM 调用流程。
    // 符合 fail-fast 原则：尽早发现无效输入，减少资源浪费。
    if (input.empty()) {
        spdlog::warn("[Agent] 收到空输入，已忽略");
        tracer_.record("error", 0, 0, {{"reason", "空输入被拒绝"}});
        return {};
    }

    // ── 步骤 2: 状态守卫 ──
    // StateMachine 确保 Agent 只在合法状态（Idle / WaitingUser）接收输入。
    // 若处于 Error 状态则自动尝试恢复；若处于 Fatal / Thinking 等非法状态则拒绝。
    if (!state_.canAcceptInput()) {
        spdlog::warn("[Agent] 当前状态 [{}] 不接受新输入", agentStateName(state_.current()));
        tracer_.record("error", 0, 0, {
            {"reason", "状态不允许输入"},
            {"state", agentStateName(state_.current())}
        });
        if (state_.isError()) {
            spdlog::info("[Agent] 尝试从错误状态自动恢复 → Idle");
            state_.recover();
        } else {
            return {};
        }
    }

    // ── 步骤 3: 轨迹记录（Fix #3）──
    // 记录用户输入的前 200 字符到 ExecutionTracer，
    // 供后续调试、日志审计和可观测性面板使用。
    tracer_.record("user_input", 0, 0, {{"input", input.substr(0, 200)}});

    // ── 步骤 4: 状态转换 Idle → Thinking（Fix #6）──
    // 通知状态机 Agent 进入思考阶段。
    // 外部组件（如 StreamDisplay）可据此切换 UI 提示状态。
    state_.transition(AgentState::Thinking);

    // ── 步骤 5: 核心处理 ──
    // 委托给 IMessageProcessor 策略对象（SerialProcessor 或 ParallelProcessor）。
    //
    // SerialProcessor 内部流程:
    //   a) 构建有效系统提示词（拼接 ContextManager 动态上下文）
    //   b) 将用户输入追加到 conversation_
    //   c) 调用 LLM（chat 接口）
    //   d) 若响应含 tool_call → 执行工具 → 将结果追加到 conversation_ → 再次调用 LLM
    //   e) 重复步骤 d) 直到 LLM 返回文本回复或达到 max_tool_rounds_ 上限
    //
    // conversation_ 通过引用传入，processor_ 内部自动管理消息追加，
    // 无需在 processUserMessage 层手动操作对话历史。
    auto t_start = std::chrono::steady_clock::now();
    auto result = processor_->process(input, conversation_, std::move(callbacks));
    auto t_end = std::chrono::steady_clock::now();
    int total_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());

    // ── 步骤 6: 状态恢复 Thinking → Idle（Fix #6）──
    // 无论处理成功或失败，都将状态机恢复到 Idle，准备接收下一条输入。
    // 若过程中发生了可恢复错误（如 LLM 超时、工具执行异常），
    // state_.isError() 在 processor_->process() 内部被置位，
    // 但此处统一过渡到 Idle，等待下一次用户输入重新触发。
    if (state_.isError()) {
        spdlog::info("[Agent] 处理完成（含可恢复错误），状态重置为 Idle");
        state_.transition(AgentState::Idle);
    } else {
        state_.transition(AgentState::Idle);
    }

    // ── 步骤 7: 轨迹记录（Fix #3）──
    // 记录最终响应的 token 消耗，用于统计和使用量监控。
    tracer_.record("done", result.raw_response.total_tokens, total_ms);

    // ── 步骤 8: 返回结果 ──
    // LLMResponse 包含:
    //   - text:              最终回复文本
    //   - total_tokens:      本次请求的总 token 消耗
    //   - tool_calls:        执行过的工具调用列表
    //   - finish_reason:     结束原因（stop / tool_calls / length 等）
    return result.raw_response;
}

// ============================================================================
// execute — 单次命令
// ============================================================================

llm::LLMResponse Agent::execute(const std::string& command,
                                 llm::StreamCallbacks callbacks)
{
    state_.transition(AgentState::Thinking);

    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();
    std::string effective_prompt = system_prompt_;
    if (context_manager_) {
        llm::Conversation tempConv;
        tempConv.addUser(command);
        auto assembly = context_manager_->assemble(tempConv, context_window_);
        if (!assembly.system_prompt.empty())
            effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
    }

    auto response = client_->chat(messages, tools, effective_prompt, std::move(callbacks));
    state_.transition(AgentState::Idle);
    return response;
}

} // namespace agent
