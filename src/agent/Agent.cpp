#include "agent/Agent.h"
#include "agent/AgentOrchestrator.h"
#include "agent/ContextManager.h"
#include "agent/PromptComposer.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>

namespace agent {

// ===========================================================================
// 构造 / 配置
// ===========================================================================

Agent::Agent(llm::ILLMClient& client, ToolRegistry& registry)
    : client_(client)
    , registry_(registry)
{
}

Agent::~Agent() = default;  // 在 .cpp 中定义（orchestrator_ 需要完整类型）

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

    // 并行编排：由 Orchestrator 内部策略判断
    if (orchestrator_ && orchestrator_->isParallelEnabled()) {
        auto result = orchestrator_->processMessage(input);
        llm::LLMResponse resp;
        resp.content = result;
        conversation_.addUser(input);
        conversation_.addAssistant(result);
        return resp;
    }

    conversation_.addUser(input);
    auto response = runToolLoop(std::move(callbacks));

    if (!response.content.empty() || !response.tool_calls.empty()) {
        conversation_.add(makeAssistantMessage(response));
    }

    return response;
}

void Agent::enableParallel(TemplateManager* templateMgr) {
    orchestrator_ = std::make_unique<AgentOrchestrator>(
        client_, registry_, system_prompt_);
    if (templateMgr) orchestrator_->setTemplateManager(templateMgr);
    spdlog::info("[Agent] 并行编排已启用");
}

void Agent::disableParallel() {
    orchestrator_.reset();
    spdlog::info("[Agent] 并行编排已禁用");
}

bool Agent::isParallelEnabled() const {
    return orchestrator_ != nullptr && orchestrator_->isParallelEnabled();
}

// ===========================================================================
// execute — 单次命令模式
// ===========================================================================

llm::LLMResponse Agent::execute(const std::string& command,
                                 llm::StreamCallbacks callbacks)
{
    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();

    // --exec 模式也使用 ContextManager（如果已设置）
    std::string effective_prompt = system_prompt_;
    if (context_manager_) {
        llm::Conversation tempConv;
        tempConv.addUser(command);
        auto assembly = context_manager_->assemble(tempConv, context_window_);
        if (!assembly.system_prompt.empty()) {
            effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
        }
    }

    return client_.chat(messages, tools, effective_prompt, std::move(callbacks));
}

// ===========================================================================
// runToolLoop — tool call 循环
// ===========================================================================

llm::LLMResponse Agent::runToolLoop(llm::StreamCallbacks callbacks)
{
    auto tools = registry_.getToolDefinitions();
    ToolPipeline pipeline(registry_, conversation_);

    // 首轮：流式调用
    std::vector<llm::Message> effective_messages;
    auto effective_prompt = buildEffectivePrompt(effective_messages);
    auto response = client_.chat(
        effective_messages, tools, effective_prompt, std::move(callbacks));

    // 后续轮次：非流式 + tool call 循环
    for (int round = 0; round < max_tool_rounds_; ++round) {
        if (response.tool_calls.empty()) {
            spdlog::debug("[Agent] 循环结束 (round={})", round);
            return response;
        }

        spdlog::info("[Agent] {} 个工具调用 (round={})",
                     response.tool_calls.size(), round);

        conversation_.add(makeAssistantMessage(response));
        pipeline.executeAndAppend(response.tool_calls); // 使用 ToolPipeline

        effective_prompt = buildEffectivePrompt(effective_messages);
        response = client_.chatNonStreaming(
            effective_messages, tools, effective_prompt);
    }

    spdlog::warn("[Agent] 达到最大轮数 ({})", max_tool_rounds_);
    return response;
}

// ===========================================================================
// buildEffectivePrompt — 使用 PromptComposer 显式组装
// ===========================================================================

std::string Agent::buildEffectivePrompt(std::vector<llm::Message>& out_messages)
{
    if (!context_manager_) {
        out_messages = conversation_.messages();
        return system_prompt_;
    }

    auto assembly = context_manager_->assemble(
        conversation_, context_window_);
    out_messages = std::move(assembly.messages);

    PromptComponents pc;
    pc.personality = system_prompt_;
    pc.context = assembly.system_prompt;
    return PromptComposer::compose(pc);
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

} // namespace agent
