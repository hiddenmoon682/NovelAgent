/// Agent 实现 — Agent 最佳实践增强版 (Fix #3,#6)。

#include "agent/Agent.h"
#include "agent/AgentOrchestrator.h"
#include "agent/ContextManager.h"
#include "agent/PromptComposer.h"
#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>

namespace agent {

Agent::Agent(llm::ILLMClient& client, ToolRegistry& registry)
    : client_(client), registry_(registry)
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
    auto sp = std::make_unique<SerialProcessor>(client_, registry_, system_prompt_);
    sp->setContextManager(context_manager_);
    sp->setContextWindow(context_window_);
    sp->setMaxToolRounds(max_tool_rounds_);
    // Fix #3: 传递 tracer 给 SerialProcessor
    sp->setTracer(&tracer_);
    processor_ = std::move(sp);
}

void Agent::useParallelProcessor(TemplateManager* tm) {
    auto pp = std::make_unique<ParallelProcessor>(client_, registry_, system_prompt_);
    if (tm) pp->setTemplateManager(tm);
    processor_ = std::move(pp);
    spdlog::info("[Agent] 切换到并行处理器");
}

void Agent::setProcessor(std::unique_ptr<IMessageProcessor> p) { processor_ = std::move(p); }
bool Agent::isParallelEnabled() const {
    return dynamic_cast<ParallelProcessor*>(processor_.get()) != nullptr;
}

// ============================================================================
// Fix #6: 状态机集成
// ============================================================================

llm::LLMResponse Agent::processUserMessage(const std::string& input,
                                            llm::StreamCallbacks callbacks)
{
    if (input.empty()) { spdlog::warn("[Agent] 空输入"); return {}; }
    if (!state_.canAcceptInput()) {
        spdlog::warn("[Agent] 当前状态 {} 不接受输入", agentStateName(state_.current()));
        // 尝试从可恢复错误中恢复
        if (state_.isError()) state_.recover();
        else return {};
    }

    // Fix #3: 记录用户输入
    tracer_.record("user_input", 0, 0, {{"input", input.substr(0, 200)}});

    // Fix #6: 状态转换 Idle → Thinking
    state_.transition(AgentState::Thinking);

    auto result = processor_->process(input, conversation_, std::move(callbacks));

    // Fix #6: Thinking → Idle（或 Error）
    if (state_.isError())
        state_.transition(AgentState::Idle); // 可恢复错误，回到 Idle
    else
        state_.transition(AgentState::Idle);

    // Fix #3: 记录完成
    tracer_.record("done", result.raw_response.total_tokens, 0);

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

    auto response = client_.chat(messages, tools, effective_prompt, std::move(callbacks));
    state_.transition(AgentState::Idle);
    return response;
}

} // namespace agent
