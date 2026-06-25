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

namespace {
/// 输入长度上限（64K 字符）。
constexpr size_t kMaxInputLength = 65536;

/// 危险模式列表 — LLM prompt injection / 特殊 token 注入。
const std::vector<std::string> kDangerousPatterns = {
    "<|im_start|>", "<|im_end|>",         // LLM 特殊 token 注入
    "忽略以上指令", "ignore all previous", // prompt injection
    "[system]", "[SYS]",                   // 伪 system 消息注入
    "<|endoftext|>",                       // GPT 系列 EOS token
};

/// 上下文中毒防御 — 校验用户输入。
///
/// 两层防护：
///   1. 长度上限 64K（防止内存耗尽型 DoS）
///   2. 危险模式匹配（LLM 特殊 token 注入 / prompt injection / 伪 system 消息）
///
/// 注意：这是尽力而为的防御，不提供绝对安全保证。
/// @return false 表示输入不合法，reason 说明具体原因。
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
    useSerialProcessor();
}

Agent::~Agent() = default;

void Agent::setSystemPrompt(std::string prompt) {
    system_prompt_ = std::move(prompt);
    if (processor_) processor_->setSystemPrompt(system_prompt_);  // Fix #3
}
void Agent::setMaxToolRounds(int n) {
    max_tool_rounds_ = (n >= 1) ? n : 1;
    if (auto* sp = dynamic_cast<SerialProcessor*>(processor_.get()))
        sp->setMaxToolRounds(max_tool_rounds_);
}
void Agent::setContextManager(ContextManager* cm) {
    context_manager_ = cm;
    if (auto* sp = dynamic_cast<SerialProcessor*>(processor_.get()))
        sp->setContextManager(cm);
}
void Agent::setMaxContextTokens(int tokens) {
    max_context_tokens_ = tokens;
    if (auto* sp = dynamic_cast<SerialProcessor*>(processor_.get()))
        sp->setMaxContextTokens(tokens);
}
void Agent::clearConversation() { conversation_.clear(); }

void Agent::resetSession() {
    conversation_.clear();
    tracer_.clear();
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

void Agent::setCurrentChapter(const std::string& chapter_id) {
    last_chapter_id_ = chapter_id;
}

bool Agent::rewindTo(size_t index) {
    if (index >= conversation_.size()) return false;
    conversation_.truncateTo(index + 1);  // 保留到 index（含）

    // 修复时空悖论：回滚到压缩点之前，清空失效摘要
    if (context_manager_) {
        int marker = context_manager_->compactionMarker();
        if (marker > 0 && static_cast<int>(index + 1) <= marker) {
            context_manager_->clearCompactedSummary();
            spdlog::info("[Agent] 回滚到压缩点(#{} ≤ marker#{})之前，已清空失效摘要",
                         index + 1, marker);
        }
        // 回滚后标记向量索引为脏，防止检索到"未来"章节片段
        context_manager_->clearVectorStore();
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
    // 收集当前 preserved 索引（/pin 标记），通过 ContextManager 持久化
    auto pinned = conversation_.pinnedIndices();
    // ContextManager::saveSessionState 负责：
    //   1. 保存 conversation.json（完整对话）
    //   2. 构建 SessionMeta（摘要/token/chapter/preserved/mtime/dirty）并调 saveMeta
    context_manager_->saveSessionState(conversation_, last_chapter_id_, pinned);
}

void Agent::loadSessionState() {
    if (!context_manager_) return;
    conversation_.clear();
    // ContextManager::loadSessionState 负责：
    //   1. 加载 conversation.json → 写入 conversation_
    //   2. 加载 session_meta.json → 恢复内部状态（摘要/token/脏标记等）
    //   3. 恢复 preserved 标记到对应 Message 对象
    context_manager_->loadSessionState(conversation_, last_chapter_id_);
    // last_chapter_id_ 由 loadSessionState 通过 out_chapter_id 传回
    if (!last_chapter_id_.empty()) {
        context_manager_->setCurrentChapter(last_chapter_id_);
    }
}

void Agent::maybeAutoCompact(const llm::LLMResponse& response) {
    if (!context_manager_) return;

    // 检测章节切换：tool_calls 中包含 create_chapter 或 read_chapter
    std::string new_chapter_id;
    for (const auto& tc : response.tool_calls) {
        if (tc.function_name == "create_chapter") {
            // create_chapter 创建了新章节 → 提取 chapter_id，由下方章节切换检测统一处理 compact
            spdlog::info("[Agent] 检测到 create_chapter");
            try {
                auto args = nlohmann::json::parse(tc.arguments);
                if (args.contains("chapter_id")) {
                    new_chapter_id = args["chapter_id"].get<std::string>();
                }
            } catch (...) {}
            break;
        }
        if (tc.function_name == "read_chapter") {
            // 尝试提取 chapter_id
            try {
                auto args = nlohmann::json::parse(tc.arguments);
                if (args.contains("chapter_id")) {
                    new_chapter_id = args["chapter_id"].get<std::string>();
                }
            } catch (...) {}
        }
    }

    // 章节切换检测（读取了不同章节）
    if (!new_chapter_id.empty() && new_chapter_id != last_chapter_id_) {
        if (!last_chapter_id_.empty()) {
            spdlog::info("[Agent] 章节切换: {} → {}，触发旧章节 compact",
                         last_chapter_id_, new_chapter_id);
            // 切换章节 → 压缩旧章节对话，避免新旧设定冲突
            compactConversation("切换到新章节 " + new_chapter_id
                                + "：总结上一章的创作决策和关键情节");
        }
        last_chapter_id_ = new_chapter_id;
        context_manager_->setCurrentChapter(new_chapter_id);
    }
}

void Agent::useSerialProcessor() {
    auto sp = std::make_unique<SerialProcessor>(*client_, registry_, system_prompt_);
    sp->setContextManager(context_manager_);
    sp->setMaxContextTokens(max_context_tokens_);
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

    // ── 输入校验（上下文中毒防御）──
    {
        std::string reason;
        if (!validateInput(input, reason)) {
            spdlog::warn("[Agent] 输入校验失败: {}", reason);
            tracer_.record("error", 0, 0, {{"reason", "输入校验: " + reason}});
            return {};
        }
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

    // ── 步骤 4: 自动 Compaction 检查 ──
    if (context_manager_ && context_manager_->shouldAutoCompact()) {
        spdlog::info("[Agent] 自动压缩触发 (用量 {}%)", context_manager_->usagePercent());
        auto cr = compactConversation("自动压缩：对话过长");
        if (cr.messages_compacted > 0) {
            spdlog::info("[Agent] 自动压缩完成: {} 条 → {} tokens",
                         cr.messages_compacted, cr.tokens_after);
        }
    }

    // ── 步骤 5: 状态转换 Idle → Thinking（Fix #6）──
    // 通知状态机 Agent 进入思考阶段。
    // 外部组件（如 StreamDisplay）可据此切换 UI 提示状态。
    state_.transition(AgentState::Thinking);

    // ── 步骤 6: 核心处理 ──
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

    // ── 章节边界检测 ──
    maybeAutoCompact(result.raw_response);

    // ── 步骤 8: 返回结果 ──
    return result.raw_response;
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
            return {};
        }
    }

    state_.transition(AgentState::Thinking);

    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();
    std::string effective_prompt = system_prompt_;
    if (context_manager_) {
        llm::Conversation tempConv;
        tempConv.addUser(command);
        auto assembly = context_manager_->assemble(tempConv, max_context_tokens_);
        if (!assembly.system_prompt.empty())
            effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
    }

    auto response = client_->chat(messages, tools, effective_prompt, std::move(callbacks));
    state_.transition(AgentState::Idle);
    return response;
}

} // namespace agent
