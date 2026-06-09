/// Agent 实现 — P1 重构版（IMessageProcessor 策略）。

#include "agent/Agent.h"
#include "agent/AgentOrchestrator.h"
#include "agent/ContextManager.h"
#include "agent/PromptComposer.h"
#include "agent/ToolCallLoop.h"
#include "agent/ToolPipeline.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>

namespace agent {

// ===========================================================================
// 构造 / 配置
// ===========================================================================

Agent::Agent(llm::ILLMClient& client, ToolRegistry& registry)
    : client_(client), registry_(registry)
{
    useSerialProcessor();  // 默认串行模式
}

Agent::~Agent() = default;

void Agent::setSystemPrompt(std::string prompt)
{
    system_prompt_ = std::move(prompt);
    // 同步更新处理器中的 prompt
    if (processor_) {
        auto* sp = dynamic_cast<SerialProcessor*>(processor_.get());
        if (sp) {
            // SerialProcessor 在构造时已持有 prompt 副本，重建
            useSerialProcessor();
        }
    }
}

void Agent::setMaxToolRounds(int n)
{
    max_tool_rounds_ = (n >= 1) ? n : 1;
}

void Agent::setContextManager(ContextManager* cm) { context_manager_ = cm; }
void Agent::setContextWindow(int window) { context_window_ = window; }

void Agent::clearConversation() { conversation_.clear(); }

// ===========================================================================
// 处理器策略（P1）
// ===========================================================================

void Agent::useSerialProcessor()
{
    auto sp = std::make_unique<SerialProcessor>(client_, registry_, system_prompt_);
    sp->setContextManager(context_manager_);
    sp->setContextWindow(context_window_);
    sp->setMaxToolRounds(max_tool_rounds_);
    processor_ = std::move(sp);
}

void Agent::useParallelProcessor(TemplateManager* templateMgr)
{
    auto pp = std::make_unique<ParallelProcessor>(client_, registry_, system_prompt_);
    if (templateMgr) pp->setTemplateManager(templateMgr);
    processor_ = std::move(pp);
    spdlog::info("[Agent] 切换到并行处理器");
}

void Agent::setProcessor(std::unique_ptr<IMessageProcessor> processor)
{
    processor_ = std::move(processor);
}

bool Agent::isParallelEnabled() const
{
    return dynamic_cast<ParallelProcessor*>(processor_.get()) != nullptr;
}

// ===========================================================================
// processUserMessage — 委托 IMessageProcessor
// ===========================================================================

llm::LLMResponse Agent::processUserMessage(const std::string& input,
                                            llm::StreamCallbacks callbacks)
{
    if (input.empty()) {
        spdlog::warn("[Agent] 空输入");
        return {};
    }

    auto result = processor_->process(input, conversation_, std::move(callbacks));
    return result.raw_response;
}

// ===========================================================================
// execute — 单次命令模式
// ===========================================================================

llm::LLMResponse Agent::execute(const std::string& command,
                                 llm::StreamCallbacks callbacks)
{
    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();

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

} // namespace agent
