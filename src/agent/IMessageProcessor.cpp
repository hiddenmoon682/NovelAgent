/// IMessageProcessor 实现。

#include "agent/IMessageProcessor.h"
#include "agent/AgentOrchestrator.h"
#include "agent/ContextManager.h"
#include "agent/PromptComposer.h"
#include "agent/ToolCallLoop.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>

namespace agent {

// ===========================================================================
// SerialProcessor
// ===========================================================================

SerialProcessor::SerialProcessor(
    llm::ILLMClient& client, ToolRegistry& registry, std::string system_prompt)
    : client_(client), registry_(registry), system_prompt_(std::move(system_prompt))
{}

SerialProcessor::Result SerialProcessor::process(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    if (input.empty()) {
        spdlog::warn("[SerialProcessor] 空输入");
        return {};
    }

    conversation.addUser(input);

    auto tools = registry_.getToolDefinitions();
    std::vector<llm::Message> effective_messages;
    auto effective_prompt = buildEffectivePrompt(conversation, effective_messages);

    ToolCallLoop loop(client_, registry_, tracer_);  // Fix #3: 传递 tracer
    ToolCallLoopConfig config;
    config.max_rounds = max_tool_rounds_;
    config.all_rounds_streaming = false; // 默认首轮流式+后续非流式（兼容现有 Mock）
    config.max_repeated_calls = 3;       // Fix #2: 循环检测
    config.token_warning_threshold = 0;  // Fix #4: 默认不监控（由调用方配置）

    auto result = loop.run(conversation, tools, effective_prompt,
                           std::move(callbacks), config);

    Result r;
    r.raw_response = result.response;

    if (!result.response.content.empty() || !result.response.tool_calls.empty()) {
        llm::Message assistant;
        assistant.role = llm::MessageRole::Assistant;
        assistant.content = result.response.content;
        assistant.tool_calls = result.response.tool_calls;
        conversation.add(std::move(assistant));
        r.text = result.response.content;
    }

    return r;
}

std::string SerialProcessor::buildEffectivePrompt(
    const llm::Conversation& conversation,
    std::vector<llm::Message>& out_messages)
{
    if (!context_manager_) {
        out_messages = conversation.messages();
        return system_prompt_;
    }

    auto assembly = context_manager_->assemble(conversation, context_window_);
    out_messages = std::move(assembly.messages);

    PromptComponents pc;
    pc.personality = system_prompt_;
    pc.context = assembly.system_prompt;
    return PromptComposer::compose(pc);
}

// ===========================================================================
// ParallelProcessor
// ===========================================================================

ParallelProcessor::ParallelProcessor(
    llm::ILLMClient& client, ToolRegistry& registry, std::string system_prompt)
{
    orchestrator_ = std::make_unique<AgentOrchestrator>(
        client, registry, std::move(system_prompt));
}

ParallelProcessor::~ParallelProcessor() = default;

ParallelProcessor::Result ParallelProcessor::process(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks /*callbacks*/)
{
    auto text = orchestrator_->processMessage(input);
    conversation.addUser(input);
    conversation.addAssistant(text);

    Result r;
    r.text = text;
    return r;
}

void ParallelProcessor::setTemplateManager(TemplateManager* tm)
{
    orchestrator_->setTemplateManager(tm);
}

} // namespace agent
