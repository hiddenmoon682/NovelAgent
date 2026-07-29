#pragma once

#include "agent/core/AgentState.h"
#include "agent/context/ContextBudgetEvaluator.h"
#include "agent/context/Compactor.h"
#include "agent/context/TokenBudget.h"
#include "agent/tool/ProgressiveToolProvider.h"
#include "agent/tool/ToolPipeline.h"
#include "agent/core/ExecutionTracer.h"
#include "agent/core/CoreLoop.h"
#include "agent/core/SessionManager.h"
#include "agent/context/IMemory.h"
#include "llm/ILLMClient.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace llm {
class LLMClientFactory;
class TokenCounter;
} // namespace llm

struct Project;

namespace agent {

struct AgentExecutionConfig {
    int max_tool_rounds = 10;
    int max_repeated_calls = 3;
    bool progressive_tool_loading = true;
};

// 上下文用量快照（供 UI 展示，refreshUsage() 刷新）。
struct ContextUsage {
    int total_tokens = 0;  // 当前上下文总 token 数（含 system prompt）
    int percent = 0;       // 占模型上限的百分比（0~100）
};

class Agent {
public:
    Agent(llm::LLMClientFactory& factory, ToolRegistry& registry, llm::IMemory& memory);
    ~Agent();

    void setSystemPrompt(std::string prompt);
    // 返回延迟工具存根（静态文本，供 setup 时注入 system prompt）。
    std::string deferredToolsStub() const { return progressive_tools_.deferredToolsStub(); }
    void setExecutionConfig(AgentExecutionConfig config) { exec_config_ = config; }
    const AgentExecutionConfig& executionConfig() const { return exec_config_; }

    // 注入项目上下文（非拥有，供 ContextAssembler 构建 system prompt）。
    void setProject(const Project* p) { project_ = p; }
    // 注入 Token 预算配置（同时刷新用量快照）。
    void setTokenBudget(TokenBudget budget);
    const TokenBudget& tokenBudget() const { return budget_; }
    // 注入 Token 校准器（非拥有，可选）。
    void setCalibrator(llm::TokenCounter* cal) { calibrator_ = cal; }
    // 注入会话持久化（非拥有，可选；转发给 SessionManager）。
    void setPersistence(SessionPersistence* p) { session_manager_.setPersistence(p); }
    // 持久化访问器（会话列表查询用；未注入时返回 nullptr）。
    SessionPersistence* persistence() { return session_manager_.persistence(); }
    // 注入压缩摘要汇聚回调（可选）。每次应用压缩后携摘要文本调用，
    // 用于将会话摘要沉淀到长期记忆，避免压缩丢失的信息永久不可找回。
    void setSummarySink(std::function<void(const std::string&)> sink) {
        summary_sink_ = std::move(sink);
    }
    // 注入 system prompt 提供者（可选；转发给 SessionManager）。会话边界
    // （新建/切换/删除重载）时重新生成 prompt，使运行期变化的成分（如
    // save_skill 新增的技能目录）在下个会话生效；会话中途不重建，保持
    // KV cache 稳定。
    void setSystemPromptProvider(std::function<std::string()> provider) {
        session_manager_.setSystemPromptProvider(std::move(provider));
    }

    llm::LLMResponse process(const std::string& input,
                              llm::StreamCallbacks callbacks = {});
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    const llm::IMemory& memory() const { return memory_; }
    void clearMemory();
    // ── 会话管理（薄转发到 SessionManager，公有 API 保持不变）──

    // 新建会话：保存当前会话（保留在列表中），创建空会话并切换。
    // 当前会话为空时不新建（避免堆积空会话），仅重置运行时状态。
    void resetSession() { session_manager_.resetSession(); }
    // 切换到指定会话：保存当前会话后重载目标会话的消息。
    bool switchSession(const std::string& id) { return session_manager_.switchSession(id); }
    // 删除指定会话（持久层负责归档）；删除的是 active 会话时自动重载新 active。
    bool deleteSession(const std::string& id) { return session_manager_.deleteSession(id); }

    // ── 上下文管理 ──
    CompactionResult compactConversation(std::optional<std::string> focus = std::nullopt);
    bool pinMessage(size_t index) { return session_manager_.pinMessage(index); }
    bool unpinMessage(size_t index) { return session_manager_.unpinMessage(index); }
    bool editMessage(size_t index, std::string new_content) {
        return session_manager_.editMessage(index, std::move(new_content));
    }
    std::vector<std::string> contextWarnings() const { return last_warnings_; }

    // ── 对话回滚 ──
    bool rewindTo(size_t index) { return session_manager_.rewindTo(index); }
    std::vector<size_t> checkpointIndices() const { return session_manager_.checkpointIndices(); }

    // ── 会话持久化 ──
    void saveSessionState() { session_manager_.saveSessionState(); }
    void loadSessionState() { session_manager_.loadSessionState(); }

    // ── 上下文用量（UI 展示）──
    ContextUsage contextUsage() const { return usage_; }

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
    void applyCompaction(const CompactionResult& cr);
    // 重新评估上下文用量并缓存到 usage_。
    void refreshUsage();

private:
    llm::LLMClientFactory& factory_;
    std::unique_ptr<llm::ILLMClient> client_;
    ToolRegistry& registry_;
    llm::IMemory& memory_;
    AgentExecutionConfig exec_config_;

    // 上下文管理（无状态组件 + 值类型，消灭可空指针）
    ContextBudgetEvaluator budget_evaluator_;
    Compactor compactor_;
    TokenBudget budget_;
    const Project* project_ = nullptr;
    llm::TokenCounter* calibrator_ = nullptr;
    std::function<void(const std::string&)> summary_sink_;   // 压缩摘要沉淀回调
    std::vector<std::string> last_warnings_;
    ContextUsage usage_;

    ProgressiveToolProvider progressive_tools_;
    ToolPipeline pipeline_;

    ExecutionTracer tracer_;
    StateMachine state_;

    // 会话管理（与 Agent 共享 memory_ 引用；构造时注入边界清理/用量刷新回调）
    SessionManager session_manager_;

    std::atomic<bool> cancel_requested_{false};
};

} // namespace agent
