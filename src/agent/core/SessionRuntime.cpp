// SessionRuntime 实现 — 每会话独立运行时。

#include "agent/core/SessionRuntime.h"
#include "agent/core/CoreLoop.h"
#include "agent/session/SessionPersistence.h"
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
            spdlog::warn("[SessionRuntime] 检测到可疑输入模式: {}", pattern);
            reason = "输入包含不允许的内容";
            return false;
        }
    }
    return true;
}
} // namespace

SessionRuntime::SessionRuntime(const std::string& session_id,
                               llm::LLMClientFactory& factory,
                               ToolRegistry& registry,
                               SessionRuntimeDeps deps)
    : session_id_(session_id)
    , client_(factory.create())
    , progressive_tools_(registry)
    , pipeline_(progressive_tools_, /*num_threads=*/2)
    , factory_(factory)
    , registry_(registry)
{
    // P5：核心依赖构造注入（一次性），后续不再 setter 修改。
    persistence_ = deps.persistence;
    calibrator_ = deps.calibrator;
    exec_config_ = deps.exec_config;
    config_ = std::move(deps.config);
    budget_.model_limit = deps.model_limit;

    // E1：会话创建/物化时经 provider 重建 prompt（读到最新技能目录，save_skill 下个会话可见）。
    // provider 为空或抛异常时回退构造时注入的 system_prompt 兜底；会话中途不重建（保 KV cache）。
    std::string prompt;
    if (config_.system_prompt_provider) {
        try {
            prompt = config_.system_prompt_provider();
        } catch (const std::exception& e) {
            spdlog::warn("[SessionRuntime] 会话 {} 重建 prompt 失败，使用兜底: {}", session_id_, e.what());
            prompt = std::move(deps.system_prompt);
        }
    } else {
        prompt = std::move(deps.system_prompt);
    }
    memory_.setSystemPrompt(std::move(prompt));
}

void SessionRuntime::refreshUsage() {
    auto eval = budget_evaluator_.evaluate(memory_, budget_, memory_.systemPrompt(),
                                           client_->config().model, calibrator_);
    std::lock_guard<std::mutex> lk(usage_mutex_);
    usage_ = ContextUsage{eval.total_tokens, budget_.usagePercent(eval.total_tokens)};
}

llm::ILLMClient& SessionRuntime::client() {
    // D6 空闲休眠：client 已释放（休眠）则懒重建
    if (!client_) {
        client_ = factory_.create();
        spdlog::info("[SessionRuntime] 会话 {} 休眠唤醒，重建 client", session_id_);
    }
    return *client_;
}

void SessionRuntime::saveSessionState() {
    if (!persistence_) return;
    if (memory_.messages().empty()) return;  // 空会话不落盘（方案 C：未发消息不产生文件）
    try {
        persistence_->save(session_id_, memory_);
        persisted_ = true;
    } catch (const std::exception& e) {
        spdlog::warn("[SessionRuntime] 会话 {} 落盘失败: {}", session_id_, e.what());
    }
}

void SessionRuntime::loadSessionState() {
    if (!persistence_) return;
    try {
        auto loaded = persistence_->load(session_id_);
        if (!loaded.messages().empty()) {
            const std::string prompt = memory_.systemPrompt();  // 保留当前 system prompt
            memory_ = std::move(loaded);
            memory_.setSystemPrompt(prompt);
            persisted_ = true;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[SessionRuntime] 会话 {} 恢复失败（从空会话开始）: {}", session_id_, e.what());
    }
}

// ── 消息级操作（收敛后：操作自身内存）──

bool SessionRuntime::rewindTo(size_t index) {
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
        spdlog::warn("[SessionRuntime] 会话 {} 回滚到 #{} 将丢弃 {} 条 pinned 消息 ({})",
                     session_id_, index, lost_pins.size(), ids);
    }

    memory_.truncateTo(index + 1);
    spdlog::info("[SessionRuntime] 会话 {} 回滚到消息 #{} (保留 {} 条)",
                 session_id_, index, memory_.size());
    return true;
}

std::vector<size_t> SessionRuntime::checkpointIndices() const {
    std::vector<size_t> result;
    const auto& all = memory_.all();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].role == llm::MessageRole::User) result.push_back(i);
    }
    return result;
}

CompactionResult SessionRuntime::compactConversation(std::optional<std::string> focus) {
    auto cr = compactor_.compact(memory_.messages(), *client_, std::move(focus));
    if (cr.messages_compacted > 0) {
        applyCompaction(cr);
    }
    return cr;
}

// ===========================================================================
// processSerial — LLM tool_call 循环处理（作用于本会话）
// ===========================================================================

SessionRuntime::InternalResult SessionRuntime::processSerial(
    const std::string& input,
    llm::StreamCallbacks callbacks)
{
    memory_.addUser(input);

    // 配置渐进式工具加载
    progressive_tools_.setEnabled(exec_config_.progressive_tool_loading);

    const std::string& effective_system_prompt = memory_.systemPrompt();

    // 发送前评估上下文用量，超限则自动压缩
    auto eval = budget_evaluator_.evaluate(memory_, budget_, effective_system_prompt,
                                           client_->config().model, calibrator_);
    last_warnings_ = eval.warnings;
    if (eval.status >= ContextStatus::AutoCompact) {
        spdlog::info("[SessionRuntime] 发送前上下文已达 {}%, 自动压缩旧历史",
                     budget_.usagePercent(eval.total_tokens));
        auto cr = compactor_.compact(memory_.messages(), *client_,
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

    config.hooks.on_round_complete = [this](int input_tok, int output_tok, int estimated) {
        if (calibrator_ && estimated > 0 && input_tok > 0) {
            calibrator_->calibrate(client_->config().model, estimated, input_tok);
        }
        int total = llm::TokenCounter::countMessagesCalibrated(
            memory_.messages(), client_->config().model, calibrator_);
        if (budget_.needsCompaction(total)) {
            spdlog::info("[SessionRuntime] 工具循环中上下文较高 ({}%), 自动压缩旧历史",
                         budget_.usagePercent(total));
            auto cr = compactor_.compact(memory_.messages(), *client_,
                "自动压缩：工具调用循环中上下文不足");
            if (cr.messages_compacted > 0) {
                applyCompaction(cr);
            }
        }
    };

    config.hooks.on_tool_results_applied = [this]() -> bool {
        auto ev = budget_evaluator_.evaluate(memory_, budget_, memory_.systemPrompt(),
                                             client_->config().model, calibrator_);
        if (ev.status < ContextStatus::AutoCompact) return true;
        spdlog::info("[SessionRuntime] 工具结果回填后上下文已达 {}%, 自动压缩旧历史",
                     budget_.usagePercent(ev.total_tokens));
        auto cr = compactor_.compact(memory_.messages(), *client_,
            "自动压缩：工具结果回填后上下文超限");
        if (cr.messages_compacted > 0) {
            applyCompaction(cr);
        }
        auto after = budget_evaluator_.evaluate(memory_, budget_, memory_.systemPrompt(),
                                                client_->config().model, calibrator_);
        return after.status < ContextStatus::Error;
    };

    // 渐进式：每轮动态获取工具列表
    auto result = loop.run(memory_, effective_system_prompt,
                           std::move(callbacks), config);

    InternalResult r;
    r.raw_response = result.response;
    if (result.cancelled)
        r.raw_response.finish_reason = "cancelled";
    else if (result.loop_detected)
        r.raw_response.finish_reason = "loop_detected";
    else if (result.budget_exhausted)
        r.raw_response.finish_reason = "context_overflow";
    if (!result.response.content.empty() || !result.response.tool_calls.empty()) {
        llm::Message assistant;
        assistant.role = llm::MessageRole::Assistant;
        assistant.content = result.response.content;
        assistant.reasoning_content = result.response.reasoning_content;
        assistant.tool_calls = result.response.tool_calls;
        memory_.inject(std::move(assistant));
        r.text = result.response.content;
    }
    return r;
}

void SessionRuntime::applyCompaction(const CompactionResult& cr) {
    auto snapshot = memory_.checkpoint();
    memory_.clear();
    for (auto& msg : cr.retained) {
        memory_.inject(std::move(msg));
    }
    memory_.setSystemPrompt(snapshot.system_prompt);
    spdlog::info("[SessionRuntime] 压缩已应用: {} 条消息保留", cr.retained.size());

    // 摘要沉淀
    if (config_.summary_sink && !cr.summary.empty()) {
        try {
            config_.summary_sink(cr.summary);
        } catch (const std::exception& e) {
            spdlog::warn("[SessionRuntime] 摘要沉淀失败（不影响会话）: {}", e.what());
        }
    }

    // 完整历史归档（D4：runtime 直接持有 persistence 落盘，无 sink 回调层）
    if (persistence_ && !cr.compacted.empty()) {
        try {
            persistence_->appendHistory(session_id_, cr.compacted);
        } catch (const std::exception& e) {
            spdlog::warn("[SessionRuntime] 完整历史归档失败（不影响会话）: {}", e.what());
        }
    }
}

// ===========================================================================
// process — 会话入口
// ===========================================================================

llm::LLMResponse SessionRuntime::process(const std::string& input,
                                         llm::StreamCallbacks callbacks)
{
    // D6 空闲休眠：client 已释放（休眠）则懒重建
    if (!client_) {
        client_ = factory_.create();
        spdlog::info("[SessionRuntime] 会话 {} 休眠唤醒，重建 client", session_id_);
    }

    resetCancel();

    // 删除请求独立于取消标志：删除运行中会话时可能任务仍在队列排队，启动后此处立即退出，
    // 不被刚才的 resetCancel() 清零覆盖（否则删除者会白等一整轮 LLM）。
    if (delete_requested_.load()) {
        spdlog::info("[SessionRuntime] 会话 {} 已标记删除，跳过本轮执行", session_id_);
        return llm::LLMResponse{.finish_reason = "cancelled"};
    }

    if (input.empty()) {
        spdlog::warn("[SessionRuntime] 收到空输入，已忽略");
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "空输入被拒绝"});
        return llm::LLMResponse{.finish_reason = "empty_input"};
    }

    {
        std::string reason;
        if (!validateInput(input, reason)) {
            spdlog::warn("[SessionRuntime] 输入校验失败: {}", reason);
            tracer_.record("error", 0, 0, ErrorPayload{.reason = "输入校验: " + reason});
            return llm::LLMResponse{.finish_reason = "invalid_input"};
        }
    }

    if (!state_.canAcceptInput()) {
        spdlog::warn("[SessionRuntime] 当前状态 [{}] 不接受新输入", agentStateName(state_.current()));
        tracer_.record("error", 0, 0, ErrorPayload{
            .reason = "状态不允许输入",
            .state = agentStateName(state_.current())
        });
        if (state_.isError()) {
            spdlog::info("[SessionRuntime] 尝试从错误状态自动恢复 → Idle");
            state_.recover();
        } else {
            return llm::LLMResponse{.finish_reason = "state_rejected"};
        }
    }

    tracer_.record("user_input", 0, 0, UserInputPayload{.input = input.substr(0, 200)});

    auto memory_snapshot = memory_.checkpoint();

    running_ = true;
    state_.transition(AgentState::Thinking);

    try {
        auto t_start = std::chrono::steady_clock::now();

        InternalResult result = processSerial(input, std::move(callbacks));

        auto t_end = std::chrono::steady_clock::now();
        int total_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());

        state_.transition(AgentState::Idle);
        running_ = false;

        tracer_.record("done", result.raw_response.total_tokens, total_ms);

        saveSessionState();  // 每轮结束按本会话 id 落盘（D3）

        refreshUsage();

        return result.raw_response;
    } catch (const std::exception& e) {
        spdlog::error("[SessionRuntime] 处理异常，强制状态恢复: {}", e.what());
        tracer_.record("error", 0, 0, ErrorPayload{.reason = "处理异常: " + std::string(e.what())});
        memory_.restore(memory_snapshot);
        state_.transition(AgentState::Error);
        state_.recover();
        running_ = false;
        return llm::LLMResponse{.finish_reason = "error"};
    }
}

} // namespace agent