#include "agent/core/Agent.h"
#include "agent/session/SessionPersistence.h"
#include "agent/core/CoreLoop.h"
#include "agent/context/Memory.h"
#include "llm/LLMClientFactory.h"
#include "llm/TokenCounter.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace agent {

namespace {
constexpr size_t kMaxInputLength = 65536;

const std::vector<std::string> kDangerousPatterns = {
    "<|im_start|>", "<|im_end|>",
    "忽略以上指令", "ignore all previous",
    "[system]", "[SYS]",
    "<|endoftext|>",
};

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

Agent::Agent(llm::LLMClientFactory& factory, ToolRegistry& registry, llm::IMemory& memory)
    : factory_(factory), client_(factory.create()), registry_(registry), memory_(memory)
    , progressive_tools_(registry)
    , pipeline_(progressive_tools_)
{
}

Agent::~Agent() = default;

void Agent::setSystemPrompt(std::string prompt) {
    memory_.setSystemPrompt(std::move(prompt));
}

void Agent::clearMemory() { memory_.clear(); }

void Agent::setTokenBudget(TokenBudget budget) {
    budget_ = budget;
    refreshUsage();  // 预算变化后百分比需重算（启动时也借此建立初始用量）
}

void Agent::resetSession() {
    // 多会话语义：旧会话保留在列表中（不归档），新建空会话并激活。
    // 当前会话为空时不新建，避免反复点击堆积空会话。
    if (persistence_ && !memory_.messages().empty()) {
        try {
            persistence_->save(memory_);       // 保存当前会话，保留在列表中
            persistence_->createSession();     // 新建空会话并设为 active（已落盘空数组）
        } catch (const std::exception& e) {
            spdlog::warn("[Agent] 新建会话落盘失败（继续重置内存）: {}", e.what());
        }
    }

    // 保留 system prompt：它由 NovelAgentApp 装配（人格/工具指令/技能），
    // 只在构造时注入一次，clear() 不能连它一起清掉
    std::string prompt = memory_.systemPrompt();
    memory_.clear();
    memory_.setSystemPrompt(std::move(prompt));

    tracer_.clear();
    last_warnings_.clear();
    progressive_tools_.reset();
    refreshUsage();
}

bool Agent::switchSession(const std::string& id) {
    if (!persistence_) return false;
    try {
        if (id == persistence_->activeSessionId()) return true;
        persistence_->save(memory_);  // 切走前保存当前会话
        if (!persistence_->switchSession(id)) return false;
    } catch (const std::exception& e) {
        spdlog::warn("[Agent] 切换会话失败: {}", e.what());
        return false;
    }
    reloadActiveSession();
    return true;
}

bool Agent::deleteSession(const std::string& id) {
    if (!persistence_) return false;
    bool was_active = false;
    try {
        was_active = (id == persistence_->activeSessionId());
        if (!persistence_->deleteSession(id)) return false;
    } catch (const std::exception& e) {
        spdlog::warn("[Agent] 删除会话失败: {}", e.what());
        return false;
    }
    // 删除非 active 会话不影响当前对话；删除 active 时持久层已切好新 active，重载即可
    if (was_active) reloadActiveSession();
    return true;
}

void Agent::reloadActiveSession() {
    std::string prompt = memory_.systemPrompt();
    memory_.clear();
    memory_.setSystemPrompt(std::move(prompt));

    tracer_.clear();
    last_warnings_.clear();
    progressive_tools_.reset();

    loadSessionState();  // 从 active 会话恢复消息（空会话则保持空，内部已刷新用量）
}

// ===========================================================================
// Compaction — 纯变换 + Agent 决定 apply
// ===========================================================================

CompactionResult Agent::compactConversation(std::optional<std::string> focus) {
    auto cr = compactor_.compact(memory_.messages(), *client_, std::move(focus));
    if (cr.messages_compacted > 0) {
        applyCompaction(cr);
    }
    // 转换为调用方可用的结果格式
    return cr;
}

void Agent::applyCompaction(const CompactionResult& cr) {
    auto snapshot = memory_.checkpoint();
    memory_.clear();
    for (auto& msg : cr.retained) {
        memory_.inject(std::move(msg));
    }
    memory_.setSystemPrompt(snapshot.system_prompt);
    spdlog::info("[Agent] 压缩已应用: {} 条消息保留", cr.retained.size());

    // 摘要沉淀：让被压缩掉的信息进入长期记忆，而非永久丢失
    if (summary_sink_ && !cr.summary.empty()) {
        try {
            summary_sink_(cr.summary);
        } catch (const std::exception& e) {
            spdlog::warn("[Agent] 摘要沉淀失败（不影响会话）: {}", e.what());
        }
    }
}

// ===========================================================================
// Memory 操作
// ===========================================================================

bool Agent::pinMessage(size_t index) {
    return memory_.pin(index);
}

bool Agent::unpinMessage(size_t index) {
    return memory_.unpin(index);
}

bool Agent::editMessage(size_t index, std::string new_content) {
    return memory_.edit(index, std::move(new_content));
}

// ===========================================================================
// 对话回滚
// ===========================================================================

bool Agent::rewindTo(size_t index) {
    if (index >= memory_.size()) return false;

    auto pinned = memory_.pinnedIndices();
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
        spdlog::warn("[Agent] 回滚到 #{} 将丢弃 {} 条 pinned 消息 ({})",
                     index, lost_pins.size(), ids);
    }

    memory_.truncateTo(index + 1);
    spdlog::info("[Agent] 回滚到消息 #{} (保留 {} 条)", index, memory_.size());
    return true;
}

std::vector<size_t> Agent::checkpointIndices() const {
    std::vector<size_t> result;
    const auto& all = memory_.all();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].role == llm::MessageRole::User) {
            result.push_back(i);
        }
    }
    return result;
}

// ===========================================================================
// 会话持久化
// ===========================================================================

void Agent::saveSessionState() {
    if (!persistence_) return;
    persistence_->save(memory_);
}

void Agent::loadSessionState() {
    if (!persistence_) return;
    auto loaded = persistence_->load();
    if (!loaded.messages().empty()) {
        // 只恢复对话消息；system prompt 以本次启动装配的为准（文件中也不存储 system）
        auto snapshot = loaded.checkpoint();
        snapshot.system_prompt = memory_.systemPrompt();
        memory_.restore(snapshot);
    }
    refreshUsage();
}

void Agent::refreshUsage() {
    auto eval = budget_evaluator_.evaluate(memory_, budget_, memory_.systemPrompt(),
                                           client_->config().model, calibrator_);
    usage_ = ContextUsage{eval.total_tokens, budget_.usagePercent(eval.total_tokens)};
}



// ===========================================================================
// processSerial — LLM tool_call 循环处理
// ===========================================================================

Agent::InternalResult Agent::processSerial(
    const std::string& input,
    llm::IMemory& memory,
    llm::StreamCallbacks callbacks)
{
    memory.addUser(input);

    // 配置渐进式工具加载（控制 getDefinitions 暴露哪些工具）
    progressive_tools_.setEnabled(exec_config_.progressive_tool_loading);

    // system prompt 已在 setup 时完整注入 memory（含静态延迟工具存根），直接使用
    const std::string& effective_system_prompt = memory.systemPrompt();

    // 发送前评估上下文用量（含完整 system prompt），超限则自动压缩
    auto eval = budget_evaluator_.evaluate(memory_, budget_, effective_system_prompt,
                                           client_->config().model, calibrator_);
    last_warnings_ = eval.warnings;
    if (eval.status >= ContextStatus::AutoCompact) {
        spdlog::info("[Agent] 发送前上下文已达 {}%, 自动压缩旧历史",
                     budget_.usagePercent(eval.total_tokens));
        auto cr = compactor_.compact(memory.messages(), *client_,
            "自动压缩：发送前上下文超限");
        if (cr.messages_compacted > 0) {
            applyCompaction(cr);
        }
    }

    CoreLoop loop(*client_, progressive_tools_, pipeline_, &state_);
    loop.setCancelled(&cancel_requested_);
    CoreLoopConfig config;
    config.setMaxRounds(exec_config_.max_tool_rounds)
          .setMaxRepeatedCalls(exec_config_.max_repeated_calls);

    config.hooks.on_round_complete = [this, &memory](int input_tok, int output_tok, int estimated) {
        if (calibrator_ && estimated > 0 && input_tok > 0) {
            calibrator_->calibrate(client_->config().model, estimated, input_tok);
        }
        int total = llm::TokenCounter::countMessagesCalibrated(
            memory.messages(), client_->config().model, calibrator_);
        if (budget_.needsCompaction(total)) {
            spdlog::info("[Agent] 工具循环中上下文较高 ({}%), 自动压缩旧历史",
                         budget_.usagePercent(total));
            auto cr = compactor_.compact(memory.messages(), *client_,
                "自动压缩：工具调用循环中上下文不足");
            if (cr.messages_compacted > 0) {
                applyCompaction(cr);
            }
        }
    };

    // 渐进式：每轮动态获取工具列表
    auto result = loop.run(memory, effective_system_prompt,
                           std::move(callbacks), config);

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
        memory.inject(std::move(assistant));
        r.text = result.response.content;
    }
    return r;
}

// ===========================================================================
// process — Agent 核心入口
// ===========================================================================

llm::LLMResponse Agent::process(const std::string& input,
                                            llm::StreamCallbacks callbacks)
{
    resetCancel();

    if (input.empty()) {
        spdlog::warn("[Agent] 收到空输入，已忽略");
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "空输入被拒绝"});
        return llm::LLMResponse{.finish_reason = "empty_input"};
    }

    {
        std::string reason;
        if (!validateInput(input, reason)) {
            spdlog::warn("[Agent] 输入校验失败: {}", reason);
            tracer_.record("error", 0, 0, ErrorPayload{.reason = "输入校验: " + reason});
            return llm::LLMResponse{.finish_reason = "invalid_input"};
        }
    }

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

    tracer_.record("user_input", 0, 0, UserInputPayload{.input = input.substr(0, 200)});

    auto memory_snapshot = memory_.checkpoint();

    state_.transition(AgentState::Thinking);

    try {
        auto t_start = std::chrono::steady_clock::now();

        InternalResult result = processSerial(input, memory_, std::move(callbacks));

        auto t_end = std::chrono::steady_clock::now();
        int total_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());

        state_.transition(AgentState::Idle);

        tracer_.record("done", result.raw_response.total_tokens, total_ms);

        try {
            saveSessionState();  // 每轮结束全量落盘（原子写）
        } catch (const std::exception& e) {
            spdlog::warn("[Agent] 会话落盘失败（不影响本轮回复）: {}", e.what());
        }

        refreshUsage();  // 本轮对话后刷新用量快照（供状态栏展示）

        return result.raw_response;
    } catch (const std::exception& e) {
        spdlog::error("[Agent] 处理异常，强制状态恢复: {}", e.what());
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "处理异常: " + std::string(e.what())});
        memory_.restore(memory_snapshot);
        state_.transition(AgentState::Error);
        state_.recover();
        return llm::LLMResponse{.finish_reason = "error"};
    }
}

// ===========================================================================
// execute — 单次命令
// ===========================================================================

llm::LLMResponse Agent::execute(const std::string& command,
                                 llm::StreamCallbacks callbacks)
{
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
    const std::string& effective_system_prompt = memory_.systemPrompt();

    last_warnings_ = budget_evaluator_.evaluate(
        memory_, budget_, effective_system_prompt,
        client_->config().model, calibrator_).warnings;

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
