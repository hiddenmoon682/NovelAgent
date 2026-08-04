#pragma once

#include "agent/core/AgentState.h"
#include "agent/core/SessionManager.h"
#include "agent/core/SessionRuntime.h"
#include "agent/context/IMemory.h"
#include "llm/ILLMClient.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace llm {
class LLMClientFactory;
class TokenCounter;
} // namespace llm

namespace agent {

// AgentExecutionConfig / ContextUsage 定义见 SessionRuntime.h。

// Agent — 多会话并行门面（D2 收敛后）。
//
// 不再持有任何单会话状态（memory/client/state/tools/tracer 全部迁入 SessionRuntime），
// 只保留共享装配配置与 LLMClientFactory/ToolRegistry 引用；全部会话能力转发给
// SessionManager（会话池容器）。公有 API 与改造前一致（E6）。
class Agent {
public:
    // @param factory LLM 客户端工厂；非拥有引用，调用方保证存活期覆盖 Agent。
    // @param registry 工具注册中心；非拥有引用，存活期约定同上。
    Agent(llm::LLMClientFactory& factory, ToolRegistry& registry);
    ~Agent();

    // 设置 system prompt（D11 共享源：写入全部会话）。
    void setSystemPrompt(std::string prompt) { session_manager_.setSystemPrompt(std::move(prompt)); }
    // 返回延迟工具存根（静态文本，供 setup 时注入 system prompt）。
    std::string deferredToolsStub() { return session_manager_.currentSession()->deferredToolsStub(); }
    // 设置/读取执行配置（工具轮数上限等）。
    void setExecutionConfig(AgentExecutionConfig config) { session_manager_.setExecutionConfig(std::move(config)); }
    const AgentExecutionConfig& executionConfig() const { return session_manager_.executionConfig(); }

    // 注入模型上下文上限（token）。
    void setModelLimit(int limit) { session_manager_.setModelLimit(limit); }
    // 注入 Token 校准器（非拥有指针，可选；调用方保证存活期）。
    void setCalibrator(llm::TokenCounter* cal) { session_manager_.setCalibrator(cal); }
    // 注入会话持久化（非拥有指针，可选；转发给 SessionManager）。
    void setPersistence(SessionPersistence* p) { session_manager_.setPersistence(p); }
    // 持久化访问器（会话列表查询用；未注入时返回 nullptr）。
    SessionPersistence* persistence() { return session_manager_.persistence(); }
    // 注入压缩摘要汇聚回调（可选）。
    void setSummarySink(std::function<void(const std::string&)> sink) {
        session_manager_.setSummarySink(std::move(sink));
    }
    // 注入 system prompt 提供者（可选；转发给 SessionManager）。
    void setSystemPromptProvider(std::function<std::string()> provider) {
        session_manager_.setSystemPromptProvider(std::move(provider));
    }

    // 处理一轮用户输入（作用于当前会话；无会话则自动创建）。
    llm::LLMResponse process(const std::string& input, llm::StreamCallbacks callbacks = {}) {
        return session_manager_.process(input, std::move(callbacks));
    }

    // 共享记忆只读访问（当前会话）。
    const llm::IMemory& memory() const { return session_manager_.memory(); }
    // 清空当前会话全部对话消息与 system prompt。
    void clearMemory() { session_manager_.currentSession()->memory().clear(); }

    // ── 会话管理（薄转发到 SessionManager，公有 API 保持不变）──

    void newSession() { session_manager_.newSession(); }
    bool switchSession(const std::string& id) { return session_manager_.switchSession(id); }
    bool deleteSession(const std::string& id) { return session_manager_.deleteSession(id); }

    // ── 多会话并行池（SessionRuntime 容器）──

    // 新建一个 SessionRuntime 会话并返回其 id（方案 C：id 提前生成、不落盘）。
    std::string createSession() { return session_manager_.createSession(); }
    // 定位指定会话的 SessionRuntime（不存在返回 nullptr）。
    SessionRuntime* session(const std::string& id) { return session_manager_.session(id); }
    // 当前已建 SessionRuntime 的会话 id 列表。
    std::vector<std::string> sessionIds() const { return session_manager_.sessionIds(); }
    // 多会话 process：定位 session_id 对应 SessionRuntime 并执行（D1）。
    llm::LLMResponse process(const std::string& session_id,
                             const std::string& input,
                             llm::StreamCallbacks callbacks = {}) {
        return session_manager_.process(session_id, input, std::move(callbacks));
    }
    // 异步 process（P1）：提交共享线程池立即返回，完成回调在池线程调用。
    void submitProcess(const std::string& session_id,
                       const std::string& input,
                       llm::StreamCallbacks callbacks,
                       std::function<void(const std::string&, llm::LLMResponse)> on_complete) {
        session_manager_.submitProcess(session_id, input, std::move(callbacks), std::move(on_complete));
    }
    // 删除指定 SessionRuntime 会话（返回是否存在）。
    bool deleteSessionRuntime(const std::string& id) {
        return session_manager_.deleteSessionRuntime(id);
    }
    // 释放所有非运行会话的 client 连接（D6 空闲休眠；运行中会话不释放）。
    void releaseIdleClients() { session_manager_.releaseIdleClients(); }

    // ── 上下文管理（转发当前会话 runtime）──

    CompactionResult compactConversation(std::optional<std::string> focus = std::nullopt) {
        return session_manager_.compactConversation(std::move(focus));
    }
    bool pinMessage(size_t index) { return session_manager_.pinMessage(index); }
    bool unpinMessage(size_t index) { return session_manager_.unpinMessage(index); }
    bool editMessage(size_t index, std::string new_content) {
        return session_manager_.editMessage(index, std::move(new_content));
    }
    std::vector<std::string> contextWarnings() const { return session_manager_.contextWarnings(); }

    // ── 对话回滚（转发 SessionManager）──
    bool rewindTo(size_t index) { return session_manager_.rewindTo(index); }
    std::vector<size_t> checkpointIndices() const { return session_manager_.checkpointIndices(); }

    // ── 会话持久化（转发 SessionManager）──
    void saveSessionState() { session_manager_.saveSessionState(); }
    void loadSessionState() { session_manager_.loadSessionState(); }
    bool pendingNewSession() const { return session_manager_.pendingNewSession(); }
    bool discardPendingNewSession() { return session_manager_.discardPendingNewSession(); }

    // ── 上下文用量（UI 展示，当前会话）──
    ContextUsage contextUsage() const { return session_manager_.contextUsage(); }

    // ── 可观测性（当前会话）──
    ExecutionTracer& tracer() { return session_manager_.currentSession()->tracer(); }

    // ── 状态查询（当前会话）──
    AgentState currentState() { return session_manager_.currentSession()->currentState(); }
    const StateMachine& stateMachine() { return session_manager_.currentSession()->stateMachine(); }
    bool canAcceptInput() { return session_manager_.currentSession()->canAcceptInput(); }

    // ── 取消支持（当前会话）──
    void requestCancel() { session_manager_.currentSession()->requestCancel(); }
    std::atomic<bool>* cancelFlag() { return session_manager_.currentSession()->cancelFlag(); }
    void resetCancel() { session_manager_.currentSession()->resetCancel(); }

    // ── 底层组件访问器（当前会话；供装配/测试使用）──
    llm::ILLMClient& client() { return session_manager_.currentSession()->client(); }
    ToolRegistry& registry() { return registry_; }
    ProgressiveToolProvider& progressiveTools() {
        return session_manager_.currentSession()->progressiveTools();
    }

private:
    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;

    // 会话池容器（D2：SessionManager 收敛为 SessionPool，持有全部 SessionRuntime）
    SessionManager session_manager_;
};

} // namespace agent