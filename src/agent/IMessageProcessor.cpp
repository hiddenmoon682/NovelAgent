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

// ===========================================================================
// SerialProcessor::process — 串行 LLM tool_call 循环处理入口
//
// 完整流程：
//
//   1. 输入守卫
//      └─ 拒绝空输入，快速返回空 Result（fail-fast）
//
//   2. 追加用户消息
//      └─ 将用户输入以 user 角色加入 conversation（对话历史）
//
//   3. 准备工具列表
//      └─ 从 ToolRegistry 获取所有已注册的工具定义（name, description, parameters）
//
//   4. 构建最终提示词
//      └─ buildEffectivePrompt() 将固定 system_prompt_
//         与 ContextManager 提供的动态上下文（RAG/摘要等）拼接，
//         同时生成有效消息列表（可能经裁剪/摘要处理）
//
//   5. 配置 ToolCallLoop
//      ├─ max_rounds:        最多 tool_call 轮数（防无限循环）
//      ├─ all_rounds_streaming: 首轮流式，后续非流式（兼容 Mock 环境）
//      ├─ max_repeated_calls:   同一工具连续重复调用上限（循环检测, Fix #2）
//      └─ token_warning_threshold: token 用量告警阈值（0=不监控, Fix #4）
//
//   6. 执行 ToolCallLoop
//      └─ loop.run() 驱动 LLM ↔ 工具的多轮交互：
//         a) 调用 LLM（chat 接口）
//         b) 如果返回 tool_call → 执行对应工具 → 结果追加回 conversation
//         c) 再次调用 LLM（带上工具执行结果）
//         d) 重复 b-c 直到 LLM 返回文本回复或达到 max_rounds
//
//   7. 处理结果
//      ├─ 将 LLM 的 assistant 回复追加到 conversation（保持对话历史完整）
//      └─ 提取最终回复文本到 Result.text
//
//   8. 返回 Result
//      └─ 含最终文本 + 完整 LLMResponse（token 计数、tool_calls 详情等）
// ===========================================================================
SerialProcessor::Result SerialProcessor::process(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    // ── 步骤 1: 输入守卫 ──
    // 拒绝空字符串输入，避免无效请求进入 LLM 调用流程。
    if (input.empty()) {
        spdlog::warn("[SerialProcessor] 空输入");
        return {};
    }

    // ── 步骤 2: 追加用户消息 ──
    // 将用户输入以 user 角色存入对话历史，作为 LLM 的输入上下文之一。
    conversation.addUser(input);

    // ── 步骤 3: 准备工具列表 ──
    // 从 ToolRegistry 获取所有已注册工具的 JSON schema 定义，
    // 这些定义会随每个 LLM 请求一起发送，使 LLM 知晓可以调用哪些工具。
    auto tools = registry_.getToolDefinitions();

    // ── 步骤 4: 构建最终提示词 ──
    // buildEffectivePrompt() 内部逻辑：
    //   - 无 ContextManager：直接返回 system_prompt_，消息列表 = conversation
    //   - 有 ContextManager：调用 assemble() 做消息裁剪/摘要，
    //     然后通过 PromptComposer 将 personality + context 组合成最终提示词
    std::vector<llm::Message> effective_messages;
    auto effective_prompt = buildEffectivePrompt(conversation, effective_messages);

    // ── 步骤 5: 配置 ToolCallLoop ──
    // 创建 ToolCallLoop 实例并设置运行参数。
    ToolCallLoop loop(client_, registry_, tracer_);  // Fix #3: 传递 tracer
    ToolCallLoopConfig config;
    config.max_rounds = max_tool_rounds_;             // 最大 tool_call 轮数
    config.all_rounds_streaming = false;               // 首轮流式 + 后续非流式
    config.max_repeated_calls = 3;                     // Fix #2: 循环检测上限
    config.token_warning_threshold = 0;                // Fix #4: 默认不监控 token

    // ── 步骤 6: 执行 ToolCallLoop ──
    // loop.run() 是多轮交互的核心：
    //   第 1 轮：将 system_prompt + 消息列表 + 工具定义 发给 LLM
    //   若返回 tool_call → 执行工具 → 结果追加到 conversation → 再次调用 LLM
    //   重复直到 LLM 返回纯文本回复或达到 max_rounds
    auto result = loop.run(conversation, tools, effective_prompt,
                           std::move(callbacks), config);

    // ── 步骤 7: 处理结果 ──
    // 将 LLM 的最终响应保存到 Result，同时追加到对话历史中，
    // 以便下一轮用户输入能携带完整的上下文。
    Result r;
    r.raw_response = result.response;

    // 只有当 LLM 有实质性输出（文本或 tool_calls）时才更新对话历史。
    // 避免空响应污染 conversation（例如遇到错误时 LLM 可能返回空内容）。
    if (!result.response.content.empty() || !result.response.tool_calls.empty()) {
        llm::Message assistant;
        assistant.role = llm::MessageRole::Assistant;
        assistant.content = result.response.content;
        assistant.tool_calls = result.response.tool_calls;
        conversation.add(std::move(assistant));
        r.text = result.response.content;
    }

    // ── 步骤 8: 返回 Result ──
    // 包含最终文本和完整的 LLMResponse（含 token 计数、tool_calls 明细等）。
    return r;
}

// ===========================================================================
// SerialProcessor::buildEffectivePrompt — 构建最终发给 LLM 的系统提示词
//
// 职责：
//   将固定 system_prompt_ 与 ContextManager 提供的动态上下文（如 RAG 检索结果、
//   对话摘要、滑动窗口裁剪后的历史）拼接成最终的系统提示词，
// 并输出经过处理的消息列表（可能被裁剪或摘要过）。
//
// 两个输出：
//   1. 返回值（string）— 最终系统提示词，发给 LLM 的 system 角色消息
//   2. out_messages   — 有效消息列表（传出参数），发给 LLM 的 messages 数组
//
// 两条路径：
//
//   路径 A：无 ContextManager（context_manager_ == nullptr）
//     └─ 直接将原始 conversation 的全部消息作为 out_messages，
//        返回原始的 system_prompt_。
//        适用于简单场景或测试环境，不进行任何上下文处理。
//
//   路径 B：有 ContextManager
//     ├─ context_manager_->assemble() 执行动态上下文策略：
//     │   ├─ 可能对过长的 conversation 做滑动窗口裁剪
//     │   ├─ 可能对早期消息做摘要压缩
//     │   └─ 可能注入 RAG 检索到的外部知识
//     ├─ assembly.messages     → 处理后的消息列表（传出）
//     ├─ assembly.system_prompt → 动态生成的附加系统提示（如检索到的知识摘要）
//     └─ 最后通过 PromptComposer::compose() 将 personality（固定角色设定）
//        与 context（动态上下文）拼接为最终系统提示词。
//
// 为什么需要这个函数？
//   1. 隔离复杂性 — 将提示词构建逻辑集中在一处，便于调试和修改策略
//   2. 支持动态上下文 — 不依赖固定 system_prompt，可根据对话状态注入不同知识
//   3. 兼容性 — 无 ContextManager 时退化为简单直通模式，不影响已有功能
// ===========================================================================
std::string SerialProcessor::buildEffectivePrompt(
    const llm::Conversation& conversation,
    std::vector<llm::Message>& out_messages)
{
    // ── 路径 A：无 ContextManager（直通模式） ──
    // 不进行任何动态上下文处理，直接返回原始 system_prompt_ 和完整对话历史。
    // 适用于单元测试或未配置上下文管理器的简单场景。
    if (!context_manager_) {
        out_messages = conversation.messages();
        return system_prompt_;
    }

    // ── 路径 B：有 ContextManager（动态上下文模式） ──
    // assemble() 内部根据 max_context_tokens_ 做消息裁剪，
    // 返回组装后的消息列表和附加的系统提示词。
    // TODO 上下文压缩问题
    auto assembly = context_manager_->assemble(conversation, max_context_tokens_);
    out_messages = std::move(assembly.messages);

    // 通过 PromptComposer 将 personality（固定角色）和 context（动态上下文）拼接。
    // 例如最终结果可能是：
    //   "你是一个小说创作助手。\n\n【当前场景】用户正在创作第 3 章..."
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

ParallelProcessor::Result ParallelProcessor::process(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    Result r;

    try {
        auto text = orchestrator_->processMessage(input);
        conversation.addUser(input);
        conversation.addAssistant(text);

        r.text = text;
        r.raw_response.content = text;
        r.raw_response.finish_reason = "stop";
        // 注意：并行模式下 token 总数需汇总多个子任务 + 汇总 LLM 调用，
        // 当前无法精确统计，保持 total_tokens 为 0

        if (callbacks.on_complete) {
            callbacks.on_complete(r.raw_response);
        }
    } catch (const std::exception& e) {
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
