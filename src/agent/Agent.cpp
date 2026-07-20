// Agent 实现 — 统一处理串行与并行消息处理路径。
// 串行路径：标准 tool_call 循环（ToolCallLoop）。
// 并行路径：子任务编排（AgentOrchestrator + SubAgent）。

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

namespace {
// 输入长度上限（64K 字符）。
constexpr size_t kMaxInputLength = 65536;

// 危险模式列表 — LLM prompt injection / 特殊 token 注入。
const std::vector<std::string> kDangerousPatterns = {
    "<|im_start|>", "<|im_end|>",          // LLM 特殊 token 注入
    "忽略以上指令", "ignore all previous",  // prompt injection
    "[system]", "[SYS]",                   // 伪 system 消息注入
    "<|endoftext|>",                       // GPT 系列 EOS token
};

// 上下文中毒防御 — 校验用户输入。
//
// 两层防护：
//   1. 长度上限 64K（防止内存耗尽型 DoS）
//   2. 危险模式匹配（LLM 特殊 token 注入 / prompt injection / 伪 system 消息）
//
// 注意：这是尽力而为的防御，不提供绝对安全保证。
// false 表示输入不合法，reason 说明具体原因。
bool validateInput(const std::string& input, std::string& reason) {
    if (input.size() > kMaxInputLength) {
        reason = "输入过长（最大 64K 字符）";
        return false;
    }
    for (const auto& pattern : kDangerousPatterns) {
        if (input.find(pattern) != std::string::npos) {
            spdlog::warn("[Agent] 检测到可疑输入模式: {}", pattern);
            reason = "输入包含不允许的内容";
            return false;
        }
    }
    return true;
}
} // namespace

Agent::Agent(llm::LLMClientFactory& factory, ToolRegistry& registry)
    : factory_(factory), client_(factory.create()), registry_(registry)
{
    parallel_mode_ = false;  // 默认串行模式
}

Agent::~Agent() = default;

void Agent::setSystemPrompt(std::string prompt) {
    conversation_.setSystemPrompt(std::move(prompt));
}
void Agent::setMaxToolRounds(int n) {
    max_tool_rounds_ = (n >= 1) ? n : 1;
}
void Agent::setContextManager(ContextManager* cm) {
    context_manager_ = cm;
    // 同步模型名和校准器到 ContextManager（用于按模型区分 Token 校准）
    if (cm) {
        cm->setModelName(client_->config().model);
    }
}
void Agent::setMaxContextTokens(int tokens) {
    max_context_tokens_ = tokens;
}
void Agent::clearConversation() { conversation_.clear(); }

void Agent::resetSession() {
    conversation_.clear();
    tracer_.clear();
    orchestrator_.reset();
    // 级联到 ContextManager：清空 token 统计、压缩摘要、警告缓存、向量脏标记
    if (context_manager_) context_manager_->resetSession();
}

CompactResult Agent::compactConversation(std::optional<std::string> focus) {
    if (!context_manager_) {
        CompactResult empty;
        empty.summary = "(上下文管理器未配置)";
        return empty;
    }
    return context_manager_->compact(conversation_, *client_, std::move(focus));
}

bool Agent::pinMessage(size_t index) {
    return conversation_.pinMessage(index);
}

bool Agent::unpinMessage(size_t index) {
    return conversation_.unpinMessage(index);
}

SessionTokenState Agent::contextStats() const {
    if (context_manager_) return context_manager_->sessionStats();
    return SessionTokenState{};
}

std::vector<std::string> Agent::contextWarnings() const {
    if (context_manager_) return context_manager_->lastWarnings();
    return {};
}

bool Agent::rewindTo(size_t index) {
    if (index >= conversation_.size()) return false;

    // Issue 11: 检测回滚是否会丢失 pinned 消息
    auto pinned = conversation_.pinnedIndices();
    std::vector<size_t> lost_pins;
    for (auto pi : pinned) {
        if (pi > index) lost_pins.push_back(pi);
    }
    if (!lost_pins.empty()) {
        std::string ids;
        for (size_t i = 0; i < lost_pins.size(); ++i) {
            if (i > 0) ids += ", ";
            ids += "#" + std::to_string(lost_pins[i]);
        }
        spdlog::warn("[Agent] 回滚到 #{} 将丢弃 {} 条 pinned 消息 ({}), "
                     "其 preserved 标记将失去意义",
                     index, lost_pins.size(), ids);
    }

    conversation_.truncateTo(index + 1);  // 保留到 index（含）

    // 修复时空悖论：回滚到压缩点之前，清空失效摘要
    if (context_manager_) {
        int marker = context_manager_->compactionMarker();
        if (marker > 0 && static_cast<int>(index + 1) <= marker) {
            context_manager_->clearCompactedSummary();
            spdlog::info("[Agent] 回滚到压缩点(#{} ≤ marker#{})之前，已清空失效摘要",
                         index + 1, marker);
        }
    }

    spdlog::info("[Agent] 回滚到消息 #{} (保留 {} 条)", index, conversation_.size());
    return true;
}

std::vector<size_t> Agent::checkpointIndices() const {
    std::vector<size_t> result;
    const auto& all = conversation_.all();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].role == llm::MessageRole::User) {
            result.push_back(i);
        }
    }
    return result;
}

void Agent::saveSessionState() {
    if (!context_manager_) return;
    auto pinned = conversation_.pinnedIndices();
    context_manager_->saveSessionState(conversation_, pinned);
}

void Agent::loadSessionState() {
    if (!context_manager_) return;
    conversation_.clear();
    context_manager_->loadSessionState(conversation_);
}

void Agent::useParallelProcessor(TemplateManager* tm) {
    parallel_mode_ = true;
    if (!orchestrator_) {
        orchestrator_ = std::make_unique<AgentOrchestrator>(factory_, registry_, conversation_.systemPrompt());
    }
    if (tm) {
        orchestrator_->setTemplateManager(tm);
    }
    spdlog::info("[Agent] 切换到并行处理器");
}

// ============================================================================
// buildEffectivePrompt — 构建最终发给 LLM 的系统提示词
//
// 职责：
//   将固定 system_prompt_ 与 ContextManager 提供的动态上下文（项目上下文、
//   工具使用指南）拼接成最终的系统提示词。
//
// 两条路径：
//
//   路径 A：无 ContextManager（context_manager_ == nullptr）
//     └─ 返回原始的 system_prompt_。
//
//   路径 B：有 ContextManager
//     ├─ context_manager_->assemble() 执行动态上下文策略：
//     │   ├─ 构建项目级 system prompt
//     │   ├─ 计算实时 token 用量
//     │   ├─ 若启用自动压缩且用量超阈值 → 触发 compact() 修改 conversation
//     │   └─ 用量告警
//     └─ PromptComposer::compose() 将 personality 与 context 拼接
// ===========================================================================
std::string Agent::buildEffectivePrompt(llm::Conversation& conversation)
{
    // ── 路径 A：无 ContextManager（直通模式） ──
    if (!context_manager_) {
        return conversation.systemPrompt();
    }

    // ── 路径 B：有 ContextManager（动态上下文模式） ──
    auto assembly = context_manager_->assemble(conversation, max_context_tokens_, &*client_);

    PromptComponents pc;
    pc.personality = conversation.systemPrompt();
    pc.context = assembly.system_prompt;
    return PromptComposer::compose(pc);
}

// ===========================================================================
// processSerial — 串行 LLM tool_call 循环处理
//
// 完整流程：
//   1. 追加用户消息     — conversation.addUser(input)
//   2. 准备工具列表     — registry_.getToolDefinitions()
//   3. 构建最终提示词   — buildEffectivePrompt()
//   4. 配置 ToolCallLoop — max_rounds / 重复检测
//   5. 执行 ToolCallLoop — loop.run() 驱动 LLM ↔ 工具的多轮交互
//   6. Token 校准回传   — 对比估算值 vs API 返回值，更新 EMA 修正因子
//   7. 记录 token 消耗  — recordUsage()
//   8. 返回结果         — InternalResult {text, raw_response}
// ===========================================================================
Agent::InternalResult Agent::processSerial(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    // ── 步骤 2: 追加用户消息 ──
    conversation.addUser(input);

    // ── 步骤 3: 准备工具列表 ──
    auto tools = registry_.getToolDefinitions();

    // ── 步骤 4: 构建最终提示词 ──
    auto effective_system_prompt = buildEffectivePrompt(conversation);

    // ── 步骤 5: 配置 ToolCallLoop ──
    ToolCallLoop loop(*client_, registry_, &state_);
    loop.setCancelled(&cancel_requested_);          // 传入取消标志
    ToolCallLoopConfig config;
    config.setMaxRounds(max_tool_rounds_)
          .setMaxRepeatedCalls(3);
          
    // 每轮完成后实时更新 TokenTracker + 校准 + 接近上限时自动压缩
    config.hooks.on_round_complete = [this](int input, int output, int estimated) {
        if (!context_manager_) return;
        context_manager_->recordUsage(input, output);
        // 每轮校准：estimated 是调 LLM 前对 conversation 的原始估算，input 是 API 返回的 prompt_tokens
        if (context_manager_->hasCalibrator() && estimated > 0 && input > 0) {
            context_manager_->calibrator()->calibrate(
                client_->config().model, estimated, input);
        }
        auto status = context_manager_->checkThresholds();
        if (status.status >= ContextStatus::AutoCompact && status.status != ContextStatus::Error) {
            spdlog::info("[Agent] 工具循环中上下文较高 ({}%), 自动压缩旧历史",
                         status.usage_percent);
            context_manager_->compact(conversation_, *client_,
                "自动压缩：工具调用循环中上下文不足");
        }
    };

    // ── 步骤 6: 执行 ToolCallLoop ──
    auto result = loop.run(conversation, tools, effective_system_prompt,
                           std::move(callbacks), config);

    // ── 步骤 7: 返回结果 ──
    InternalResult r;
    r.raw_response = result.response;
    if (result.cancelled)
        r.raw_response.finish_reason = "cancelled";
    else if (result.loop_detected)
        r.raw_response.finish_reason = "loop_detected";
    if (!result.response.content.empty() || !result.response.tool_calls.empty()) {
        llm::Message assistant;
        assistant.role = llm::MessageRole::Assistant;
        assistant.content = result.response.content;
        assistant.reasoning_content = result.response.reasoning_content;
        assistant.tool_calls = result.response.tool_calls;
        conversation.add(std::move(assistant));
        r.text = result.response.content;
    }
    return r;
}

// ===========================================================================
// processParallel — 并行编排处理入口
//
// 通过 AgentOrchestrator 分解任务 → 多子 Agent 并行执行 → 汇总结果。
// ===========================================================================
Agent::InternalResult Agent::processParallel(
    const std::string& input,
    llm::Conversation& conversation,
    llm::StreamCallbacks callbacks)
{
    InternalResult r;

    // 惰性创建编排器
    if (!orchestrator_) {
        orchestrator_ = std::make_unique<AgentOrchestrator>(factory_, registry_, conversation_.systemPrompt());
    }

    // ── 上下文组装 ──
    try {
        std::string effective_system_prompt = conversation_.systemPrompt();
        if (context_manager_) {
            auto assembly = context_manager_->assemble(conversation, max_context_tokens_, nullptr);
            if (!assembly.system_prompt.empty())
                effective_system_prompt = conversation_.systemPrompt() + "\n\n" + assembly.system_prompt;
        }
        orchestrator_->setMainPrompt(effective_system_prompt);
    } catch (const std::exception& e) {
        spdlog::warn("[Agent] 并行路径上下文组装失败，使用原始 prompt: {}", e.what());
    }

    // ── 核心处理 ──
    try {
        tracer_.record("parallel_start", 0, 0,
            ParallelStartPayload{.input = input.substr(0, 200)});

        auto text = orchestrator_->processMessage(input);
        conversation.addUser(input);
        conversation.addAssistant(text);

        // 注入子任务工具调用详情
        {
            const auto& sub_tasks = orchestrator_->lastSubTasks();
            if (!sub_tasks.empty()) {
                std::string sub_detail;
                for (const auto& st : sub_tasks) {
                    sub_detail += "[" + st.id + ":" + st.status + "] " + st.description + "\n";
                    if (!st.result.empty()) {
                        sub_detail += "  结果: " + st.result.substr(0, 300);
                        if (st.result.size() > 300) sub_detail += "...";
                        sub_detail += "\n";
                    }
                    if (!st.error.empty()) {
                        sub_detail += "  错误: " + st.error + "\n";
                    }
                }
                if (!sub_detail.empty()) {
                    llm::Message ass = llm::Message::assistant(
                        "(并行分析完成，子任务详情:)\n" + sub_detail);
                    conversation.add(std::move(ass));
                }
            }
        }

        r.text = text;
        r.raw_response.content = text;
        r.raw_response.finish_reason = "stop";

        if (context_manager_) {
            int main_input = orchestrator_->lastInputTokens();
            int main_output = orchestrator_->lastOutputTokens();
            int sub_input = orchestrator_->lastSubInputTokens();
            int sub_output = orchestrator_->lastSubOutputTokens();
            context_manager_->recordUsage(
                main_input + sub_input,
                main_output + sub_output);
        }

        tracer_.record("parallel_done", r.raw_response.total_tokens, 0);

        if (callbacks.on_complete) {
            callbacks.on_complete(r.raw_response);
        }
    } catch (const std::exception& e) {
        spdlog::error("[Agent] 并行路径处理异常: {}", e.what());
        r.raw_response.finish_reason = "error";
        tracer_.record("error", 0, 0,
            ErrorPayload{.reason = "并行处理异常: " + std::string(e.what())});
        if (callbacks.on_error) {
            callbacks.on_error(e.what());
        }
    }

    return r;
}

// ============================================================================
// process — Agent 核心入口
//
// 职责：接收用户自然语言输入，根据并行标志选择处理路径，返回最终回复。
//
// 完整流程:
//   1. 输入守卫   — 拒绝空输入（fail-fast）
//   2. 状态守卫   — 检查 StateMachine 是否可接受输入；尝试从 Error 自动恢复
//   3. 轨迹记录   — ExecutionTracer 记录用户输入
//   4. 状态转换   — Idle → Thinking
//   5. 核心处理   — 串行路径（processSerial）或并行路径（processParallel）
//   6. 状态恢复   — Thinking → Idle
//   7. 轨迹记录   — 记录完成信息（token 消耗等）
//   8. 返回结果   — LLMResponse（含文本、token 计数、tool_call 详情）
// ============================================================================

llm::LLMResponse Agent::process(const std::string& input,
                                            llm::StreamCallbacks callbacks)
{
    // ── 步骤 1: 输入守卫 ──
    if (input.empty()) {
        spdlog::warn("[Agent] 收到空输入，已忽略");
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "空输入被拒绝"});
        return llm::LLMResponse{.finish_reason = "empty_input"};
    }

    // ── 输入校验（上下文中毒防御）──
    {
        std::string reason;
        if (!validateInput(input, reason)) {
            spdlog::warn("[Agent] 输入校验失败: {}", reason);
            tracer_.record("error", 0, 0, ErrorPayload{.reason = "输入校验: " + reason});
            return llm::LLMResponse{.finish_reason = "invalid_input"};
        }
    }

    // ── 步骤 2: 状态守卫 ──
    if (!state_.canAcceptInput()) {
        spdlog::warn("[Agent] 当前状态 [{}] 不接受新输入", agentStateName(state_.current()));
        tracer_.record("error", 0, 0, ErrorPayload{
            .reason = "状态不允许输入",
            .state = agentStateName(state_.current())
        });
        if (state_.isError()) {
            spdlog::info("[Agent] 尝试从错误状态自动恢复 → Idle");
            state_.recover();
        } else {
            return llm::LLMResponse{.finish_reason = "state_rejected"};
        }
    }

    // ── 步骤 3: 轨迹记录 ──
    tracer_.record("user_input", 0, 0, UserInputPayload{.input = input.substr(0, 200)});

    // ── 步骤 3.5: 对话快照 — 异常时回滚所有修改（含 compact 等不可逆操作）──
    llm::Conversation conversation_snapshot = conversation_;

    // ── 步骤 4: 状态转换 Idle → Thinking ──
    state_.transition(AgentState::Thinking);

    // ── 步骤 5-6.5: 核心处理 + 轨迹记录 + 增量保存 ──
    try {
        auto t_start = std::chrono::steady_clock::now();

        // 根据并行标志选择处理路径
        InternalResult result;
        if (parallel_mode_)
            result = processParallel(input, conversation_, std::move(callbacks));
        else
            result = processSerial(input, conversation_, std::move(callbacks));

        auto t_end = std::chrono::steady_clock::now();
        int total_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());

        // ── 步骤 6: 状态恢复 Thinking → Idle ──
        if (state_.isError()) {
            spdlog::info("[Agent] 处理完成（含可恢复错误），状态重置为 Idle");
            state_.transition(AgentState::Idle);
        } else {
            state_.transition(AgentState::Idle);
        }

        // ── 步骤 7: 轨迹记录 ──
        tracer_.record("done", result.raw_response.total_tokens, total_ms);

        // ── 步骤 7.5: 会话增量保存 ──
        try {
            saveSessionState();
        } catch (const std::exception& e) {
            spdlog::warn("[Agent] 会话增量保存失败（不影响本轮回复）: {}", e.what());
        }

        // ── 步骤 8: 返回结果 ──
        return result.raw_response;
    } catch (const std::exception& e) {
        spdlog::error("[Agent] 处理异常，强制状态恢复: {}", e.what());
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "处理异常: " + std::string(e.what())});
        conversation_ = std::move(conversation_snapshot);  // 回滚对话到处理前的状态
        state_.transition(AgentState::Error);
        state_.recover();
        return llm::LLMResponse{.finish_reason = "error"};
    }
}

// ============================================================================
// execute — 单次命令
// ============================================================================

llm::LLMResponse Agent::execute(const std::string& command,
                                 llm::StreamCallbacks callbacks)
{
    // ── 输入校验（上下文中毒防御）──
    {
        std::string reason;
        if (!validateInput(command, reason)) {
            spdlog::warn("[Agent] execute 输入校验失败: {}", reason);
            return llm::LLMResponse{.finish_reason = "invalid_input"};
        }
    }

    state_.transition(AgentState::Thinking);

    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();
    std::string effective_system_prompt = conversation_.systemPrompt();
    if (context_manager_) {
        llm::Conversation tempConv;
        tempConv.addUser(command);
        auto assembly = context_manager_->assemble(tempConv, max_context_tokens_);
        if (!assembly.system_prompt.empty())
            effective_system_prompt = conversation_.systemPrompt() + "\n\n" + assembly.system_prompt;
    }

    try {
        auto response = client_->chat(messages, tools, effective_system_prompt, std::move(callbacks));
        state_.transition(AgentState::Idle);
        return response;
    } catch (const std::exception& e) {
        spdlog::error("[Agent] execute 异常，强制状态恢复: {}", e.what());
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "execute 异常: " + std::string(e.what())});
        state_.transition(AgentState::Error);
        state_.recover();
        return llm::LLMResponse{.finish_reason = "error"};
    }
}

} // namespace agent
