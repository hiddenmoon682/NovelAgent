#pragma once

#include "agent/core/AgentState.h"
#include "agent/context/ContextAssembler.h"
#include "agent/context/Compactor.h"
#include "agent/context/TokenBudget.h"
#include "agent/tool/ProgressiveToolProvider.h"
#include "agent/tool/ToolPipeline.h"
#include "agent/core/ExecutionTracer.h"
#include "agent/core/CoreLoop.h"
#include "agent/context/IMemory.h"
#include "llm/ILLMClient.h"

#include <atomic>
#include <memory>
#include <string>

namespace llm {
class LLMClientFactory;
class TokenCounter;
} // namespace llm

struct Project;

namespace agent {

class SessionPersistence;

struct AgentExecutionConfig {
    int max_tool_rounds = 10;
    int max_repeated_calls = 3;
    bool progressive_tool_loading = true;
};

class Agent {
public:
    Agent(llm::LLMClientFactory& factory, ToolRegistry& registry, llm::IMemory& memory);
    ~Agent();

    void setSystemPrompt(std::string prompt);
    void setExecutionConfig(AgentExecutionConfig config) { exec_config_ = config; }
    const AgentExecutionConfig& executionConfig() const { return exec_config_; }

    // 注入项目上下文（非拥有，供 ContextAssembler 构建 system prompt）。
    void setProject(const Project* p) { project_ = p; }
    // 注入 Token 预算配置。
    void setTokenBudget(TokenBudget budget) { budget_ = budget; }
    const TokenBudget& tokenBudget() const { return budget_; }
    // 注入 Token 校准器（非拥有，可选）。
    void setCalibrator(llm::TokenCounter* cal) { calibrator_ = cal; }
    // 注入会话持久化（非拥有，可选）。
    void setPersistence(SessionPersistence* p) { persistence_ = p; }

    llm::LLMResponse process(const std::string& input,
                              llm::StreamCallbacks callbacks = {});
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    const llm::IMemory& memory() const { return memory_; }
    void clearMemory();
    void resetSession();

    // ── 上下文管理 ──
    CompactionResult compactConversation(std::optional<std::string> focus = std::nullopt);
    bool pinMessage(size_t index);
    bool unpinMessage(size_t index);
    bool editMessage(size_t index, std::string new_content);
    std::vector<std::string> contextWarnings() const { return last_warnings_; }

    // ── 对话回滚 ──
    bool rewindTo(size_t index);
    std::vector<size_t> checkpointIndices() const;

    // ── 会话持久化 ──
    void saveSessionState();
    void loadSessionState();

    // ── 可观测性 ──
    ExecutionTracer& tracer() { return tracer_; }
    const ExecutionTracer& tracer() const { return tracer_; }

    // ── 状态查询 ──
    AgentState currentState() const { return state_.current(); }
    const StateMachine& stateMachine() const { return state_; }
    bool canAcceptInput() const { return state_.canAcceptInput(); }

    // ── 取消支持 ──
    void requestCancel() { cancel_requested_.store(true); }
    std::atomic<bool>* cancelFlag() { return &cancel_requested_; }
    void resetCancel() { cancel_requested_.store(false); }

    llm::ILLMClient& client() { return *client_; }
    ToolRegistry& registry() { return registry_; }
    ProgressiveToolProvider& progressiveTools() { return progressive_tools_; }

private:
    struct InternalResult {
        std::string text;
        llm::LLMResponse raw_response;
    };

    InternalResult processSerial(const std::string& input,
                                 llm::IMemory& memory,
                                 llm::StreamCallbacks callbacks);
    std::string buildEffectivePrompt(llm::IMemory& memory);
    void applyCompaction(const CompactionResult& cr);

private:
    llm::LLMClientFactory& factory_;
    std::unique_ptr<llm::ILLMClient> client_;
    ToolRegistry& registry_;
    llm::IMemory& memory_;
    AgentExecutionConfig exec_config_;

    // 上下文管理（无状态组件 + 值类型，消灭可空指针）
    ContextAssembler assembler_;
    Compactor compactor_;
    TokenBudget budget_;
    const Project* project_ = nullptr;
    llm::TokenCounter* calibrator_ = nullptr;
    SessionPersistence* persistence_ = nullptr;
    std::vector<std::string> last_warnings_;

    ProgressiveToolProvider progressive_tools_;
    ToolPipeline pipeline_;

    ExecutionTracer tracer_;
    StateMachine state_;

    std::atomic<bool> cancel_requested_{false};
};

} // namespace agent
