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

void Agent::resetSession() {
    memory_.clear();
    tracer_.clear();
    last_warnings_.clear();
    progressive_tools_.reset();
}

// ===========================================================================
// Compaction — 纯变换 + Agent 决定 apply
// ===========================================================================

CompactionResult Agent::compactConversation(std::optional<std::string> focus) {
    auto cr = compactor_.compact(memory_.messages(), *client_, std::move(focus));
    if (cr.messages_compacted > 0) {
        applyCompaction(cr);
    }
    // 转换为旧接口兼容（ReplHandler 使用）
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
    memory_.clear();
    auto loaded = persistence_->load();
    memory_.restore(loaded.checkpoint());
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

    // 配置渐进式工具加载
    progressive_tools_.setEnabled(exec_config_.progressive_tool_loading);

    // 构建最终提示词（静态部分已在 setup 时注入 memory，此处只拼动态部分）
    std::string effective_system_prompt = memory.systemPrompt();
    if (progressive_tools_.isEnabled()) {
        effective_system_prompt += progressive_tools_.deferredToolsStub();
    }

    // 发送前评估上下文用量，超限则自动压缩
    auto eval = budget_evaluator_.evaluate(memory_, budget_,
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
            saveSessionState();
        } catch (const std::exception& e) {
            spdlog::warn("[Agent] 会话增量保存失败（不影响本轮回复）: {}", e.what());
        }

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
        memory_, budget_, client_->config().model, calibrator_).warnings;

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
