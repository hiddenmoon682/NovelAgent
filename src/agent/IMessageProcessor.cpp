/// IMessageProcessor 实现 — Phase 4 线程安全：ParallelProcessor 通过工厂创建独立 AgentOrchestrator。

#include "agent/IMessageProcessor.h"
#include "agent/AgentOrchestrator.h"
#include "agent/ContextManager.h"
#include "agent/PromptComposer.h"
#include "agent/ToolCallLoop.h"
#include "agent/ToolRegistry.h"
#include "llm/LLMClientFactory.h"

#include <spdlog/spdlog.h>

namespace agent {

// ===========================================================================
// SerialProcessor
// ===========================================================================

SerialProcessor::SerialProcessor(
    llm::ILLMClient& client, ToolRegistry& registry, std::string system_prompt)
    : client_(client), registry_(registry), system_prompt_(std::move(system_prompt))
{}

/// 处理单条用户输入，走串行工具调用循环。
///
/// 流程：
///   1. 空输入检查 —— 若 input 为空直接返回空结果。
///   2. 将用户消息加入对话历史。
///   3. 获取工具定义列表，并通过 buildEffectivePrompt() 构建有效的
///       system prompt 与待发送的消息列表（含上下文窗口裁剪/摘要）。
///   4. 创建 ToolCallLoop 并执行多轮工具调用循环 —— LLM 可多次调用工具，
///      每轮结果重新注入对话，直至自然结束或达到 max_rounds。
///   5. 若 LLM 返回了有效内容或工具调用，构建 Assistant 消息加入对话。
///
/// \param input         用户输入文本
/// \param conversation  对话历史（in/out，会被修改）
/// \param callbacks     流式回调（on_chunk/on_complete/on_error/on_tool_use）
/// \return Result 结构体，含 text（纯文本回复）和 raw_response（完整响应）
SerialProcessor::Result SerialProcessor::process(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    // 1) 空输入保护
    if (input.empty()) {
        spdlog::warn("[SerialProcessor] 空输入");
        return {};
    }

    // 2) 记录用户消息
    conversation.addUser(input);

    // 3) 获取工具定义 & 构建有效 prompt（含上下文管理）
    auto tools = registry_.getToolDefinitions();
    std::vector<llm::Message> effective_messages;
    auto effective_prompt = buildEffectivePrompt(conversation, effective_messages);

    // 4) 创建工具调用循环并运行
    ToolCallLoop loop(client_, registry_, tracer_);
    ToolCallLoopConfig config;
    config.max_rounds = max_tool_rounds_;
    config.all_rounds_streaming = false;    // 首轮流式，后续非流式（兼容现有 Mock）
    config.max_repeated_calls = 3;          // 重复调用检测阈值
    config.token_warning_threshold = 0;     // 默认不监控 token（由调用方配置）

    auto result = loop.run(conversation, tools, effective_prompt,
                           std::move(callbacks), config);

    // 5) 组装返回结果
    Result r;
    r.raw_response = result.response;

    // 若 LLM 产生了有效内容或工具调用，将其加入对话历史
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

/// 构建发送给 LLM 的有效 prompt（含上下文窗口管理）。
///
/// 核心职责：
///   1. 若未设置 context_manager_（无上下文管理），直接返回原始对话消息
///      和系统提示词，不做任何裁剪。
///   2. 若已设置 context_manager_，调用其 assemble() 对对话历史进行
///      上下文窗口管理 —— 将超出 context_window_ 的早期消息进行摘要压缩
///      或裁剪，返回精简后的消息列表和补充的 system prompt。
///   3. 通过 PromptComposer 将 personality（原始系统提示词）与 context
///      （上下文管理生成的补充指令）组合成最终的 system prompt。
///
/// \param conversation  完整的对话历史（只读）
/// \param out_messages  [out] 实际发送给 LLM 的消息列表
/// \return 组合后的完整 system prompt 字符串
std::string SerialProcessor::buildEffectivePrompt(
    const llm::Conversation& conversation,
    std::vector<llm::Message>& out_messages)
{
    // 无上下文管理器：直接透传原始消息和原始 system prompt
    if (!context_manager_) {
        out_messages = conversation.messages();
        return system_prompt_;
    }

    // 有上下文管理器：对对话进行裁剪/摘要，返回有效窗口内的消息
    auto assembly = context_manager_->assemble(conversation, context_window_);
    out_messages = std::move(assembly.messages);

    // 将 personality（原始角色设定）与 context（裁剪摘要补充说明）组合
    PromptComponents pc;
    pc.personality = system_prompt_;
    pc.context = assembly.system_prompt;
    return PromptComposer::compose(pc);
}

// ===========================================================================
// ParallelProcessor
// ===========================================================================

ParallelProcessor::ParallelProcessor(
    llm::LLMClientFactory& factory, ToolRegistry& registry, std::string system_prompt)
    : factory_(factory), registry_(registry), system_prompt_(std::move(system_prompt))
{
    orchestrator_ = std::make_unique<AgentOrchestrator>(factory_, registry_, system_prompt_);
}

ParallelProcessor::~ParallelProcessor() = default;

void ParallelProcessor::setSystemPrompt(const std::string& p) {
    system_prompt_ = p;
    orchestrator_ = std::make_unique<AgentOrchestrator>(factory_, registry_, system_prompt_);
}

/// 处理单条用户输入，走并行 Agent 编排模式。
///
/// 与 SerialProcessor 不同，此方法不直接驱动 ToolCallLoop，
/// 而是委托给内部的 AgentOrchestrator，由其将任务分解为多个
/// 子 Agent 并行执行，最后汇总结果。
///
/// 流程：
///   1. 调用 orchestrator_->processMessage() 分发任务并获取汇总文本。
///   2. 将用户输入和助理回复记录到 conversation。
///   3. 触发 on_complete 回调通知完成。
///   4. 异常时触发 on_error 回调并标记 finish_reason 为 "error"。
///
/// \param input         用户输入文本
/// \param conversation  对话历史（in/out，会被修改）
/// \param callbacks     流式回调
/// \return Result 结构体
ParallelProcessor::Result ParallelProcessor::process(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    Result r;

    try {
        // 1) 通过编排器并行处理（子 Agent 分解 + 汇总）
        auto text = orchestrator_->processMessage(input);
        // 2) 更新对话历史
        conversation.addUser(input);
        conversation.addAssistant(text);

        // 3) 填充结果
        r.text = text;
        r.raw_response.content = text;
        r.raw_response.finish_reason = "stop";
        // 注意：并行模式下 token 总数需汇总多个子任务 + 汇总 LLM 调用，
        // 当前无法精确统计，保持 total_tokens 为 0

        if (callbacks.on_complete) {
            callbacks.on_complete(r.raw_response);
        }
    } catch (const std::exception& e) {
        // 4) 异常处理
        spdlog::error("[ParallelProcessor] 并行处理异常: {}", e.what());
        r.raw_response.finish_reason = "error";
        if (callbacks.on_error) {
            callbacks.on_error(e.what());
        }
    }

    return r;
}

void ParallelProcessor::setTemplateManager(TemplateManager* tm)
{
    orchestrator_->setTemplateManager(tm);
}

} // namespace agent
