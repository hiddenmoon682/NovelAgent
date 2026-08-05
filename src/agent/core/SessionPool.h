#pragma once

// SessionPool — 会话池容器（D2 收敛：SessionPool 收敛为 SessionPool）。
//
// 多会话并行架构下，本类持有全部 SessionRuntime（map<session_id, unique_ptr<SessionRuntime>>）
// 与共享线程池，负责会话生命周期与消息级操作的路由。不再持有单一共享 memory——
// 每个会话的内存由各自 SessionRuntime 拥有。
//
// 职责：
//   - 会话生命周期：新建（newSession/延迟落盘）、切换（switchSession 仅切焦点）、
//     删除（deleteSessionRuntime，运行中先 cancel+wait）。
//   - 执行：process(session_id, input, cb) 提交共享线程池并行执行。
//   - 消息级操作路由：pin/unpin/edit/rewindTo/checkpointIndices 转发到当前会话 runtime。
//   - 持久化路由：saveSessionState/loadSessionState 转发到当前会话 runtime。
//   - 共享装配配置：system prompt / model limit 存储并在会话创建时注入（D11）。

#include "agent/core/SessionRuntime.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace llm {
class LLMClientFactory;
class TokenCounter;
} // namespace llm

namespace agent {

class ToolRegistry;
class SessionPersistence;
class ThreadPool;

// 会话池容器。持有所有 SessionRuntime 与共享线程池。
class SessionPool {
public:
    // @param factory LLM 客户端工厂（非拥有引用）。
    // @param registry 工具注册中心（非拥有引用）。
    SessionPool(llm::LLMClientFactory& factory, ToolRegistry& registry);
    ~SessionPool();

    // ── 会话生命周期 ──

    // 新建会话（方案 C：id 提前生成、文件延迟落盘），返回新会话 id。
    std::string createSession();
    // 新建会话并设为当前（延迟落盘；pending 语义见 pendingNewSession）。
    void newSession();
    // 是否存在未落盘的新会话（当前会话 persisted_ == false）。
    bool pendingNewSession() const;
    // 丢弃未落盘的新会话（回到最近的非 pending 会话；无则保留空当前）。
    bool discardPendingNewSession();
    // 切换到指定会话（仅切焦点，不阻塞）；id 不存在返回 false。
    bool switchSession(const std::string& id);
    // 删除指定会话（运行中先 cancel+wait）；删除当前会话自动切到剩余会话。
    bool deleteSession(const std::string& id);
    // 删除指定 SessionRuntime（返回是否存在）。
    bool deleteSessionRuntime(const std::string& id);

    // ── 池访问 ──

    // 定位指定会话 runtime（不存在返回 nullptr）。
    SessionRuntime* session(const std::string& id);
    // 物化指定会话（P8 懒物化）：已在池则仅设当前焦点；不在池且持久层存在该 id 时
    // 建 runtime + loadSessionState 恢复历史并设为当前焦点。持久层不存在或失败返回 false。
    bool materializeSession(const std::string& id);
    // 当前会话 runtime（无则自动创建）。
    SessionRuntime* currentSession();
    // 当前会话 id（无则自动创建后返回）。
    std::string currentSessionId();
    // 已建 runtime 的会话 id 列表。
    std::vector<std::string> sessionIds() const;
    // 是否存在任一运行中的会话（全局 busy 聚合信号，D12/阶段 4）。
    bool anyRunning() const;
    // 释放所有非运行会话的 client 连接（D6 空闲休眠）。
    void releaseIdleClients();

    // ── 执行 ──

    // 多会话 process：定位 session_id 对应 runtime 提交共享线程池执行（D1）。
    llm::LLMResponse process(const std::string& session_id,
                             const std::string& input,
                             llm::StreamCallbacks callbacks = {});
    // 当前会话 process（无会话则自动创建）。
    llm::LLMResponse process(const std::string& input, llm::StreamCallbacks callbacks = {});

    // 异步 process（P1）：提交共享线程池立即返回，完成时在池线程调用 on_complete(session_id, response)。
    // 用于 GUI 等不希望阻塞调用线程的场景；in-flight 仍被跟踪（删除运行中会话可 cancel+wait）。
    void submitProcess(const std::string& session_id,
                       const std::string& input,
                       llm::StreamCallbacks callbacks,
                       std::function<void(const std::string&, llm::LLMResponse)> on_complete);

    // 取消所有 in-flight 会话并等待其退场（方案 A：退出/重建前调用，防 on_complete
    // 在调用方析构后访问其成员）。每任务最多等 timeout；超时后放弃等待（残留任务由
    // 池析构 join 兜底）。返回是否所有任务在超时内退场。
    bool cancelAllAndWait(std::chrono::milliseconds timeout = std::chrono::seconds(2));

    // ── 消息级操作（转发当前会话 runtime）──

    bool pinMessage(size_t index);
    bool unpinMessage(size_t index);
    bool editMessage(size_t index, std::string new_content);
    bool rewindTo(size_t index);
    std::vector<size_t> checkpointIndices() const;
    // 手动压缩当前会话。
    CompactionResult compactConversation(std::optional<std::string> focus = std::nullopt);
    // 最近一次预算评估警告（当前会话）。
    std::vector<std::string> contextWarnings() const;
    // 当前会话上下文用量快照。
    ContextUsage contextUsage() const;
    // 当前会话对话记忆只读访问。
    const llm::IMemory& memory() const;

    // ── 持久化路由（转发当前会话 runtime）──

    void saveSessionState();
    void loadSessionState();

    // ── 装配配置（D11：共享源 + 创建时注入）──

    void setSystemPrompt(std::string prompt);
    void setModelLimit(int limit);
    // 注入持久化（新会话创建时转发给 runtime）。
    void setPersistence(SessionPersistence* p);
    // 持久化访问器（会话列表查询用；未注入时返回 nullptr）。
    SessionPersistence* persistence() { return persistence_; }
    // 注入共享校准器（新会话创建时转发给 runtime）。
    void setCalibrator(llm::TokenCounter* cal);
    // 注入压缩摘要沉淀回调（新会话创建时转发给 runtime）。
    void setSummarySink(std::function<void(const std::string&)> sink);
    // 注入共享装配配置提供者（当前未使用，保留接口）。
    void setSystemPromptProvider(std::function<std::string()> provider) {
        system_prompt_provider_ = std::move(provider);
    }
    // 设置/读取执行配置（工具轮数上限等；新会话创建时注入，D11 仅创建时生效）。
    void setExecutionConfig(AgentExecutionConfig config);
    const AgentExecutionConfig& executionConfig() const { return exec_config_; }

private:
    // 用共享源配置新建一个 SessionRuntime。
    std::unique_ptr<SessionRuntime> makeRuntime(const std::string& id);
    // 生成唯一会话 id。
    std::string makeSessionId();
    // 删除当前会话后的焦点回退（自动切到最近会话）。
    void fallbackCurrentAfterDelete();
    // 只读定位当前会话（不自动创建；无则返回 nullptr）。
    const SessionRuntime* currentSessionConst() const;
    // 无会话时的空记忆兜底（const memory() 用）。
    static const llm::IMemory& kEmptyMemory();

    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;
    SessionPersistence* persistence_ = nullptr;
    llm::TokenCounter* calibrator_ = nullptr;
    std::function<void(const std::string&)> summary_sink_;
    std::function<std::string()> system_prompt_provider_;

    // 共享装配配置（D11：创建时注入）
    std::string shared_system_prompt_;
    int shared_model_limit_ = 0;
    AgentExecutionConfig exec_config_;

    // 会话池与执行
    std::map<std::string, std::unique_ptr<SessionRuntime>> pool_;
    std::string current_session_id_;
    std::atomic<int> session_seq_{0};
    std::unique_ptr<ThreadPool> pool_exec_;

    // 进行中的 process 任务（阶段 5：删除运行中会话时 cancel+wait）
    std::map<std::string, std::shared_future<llm::LLMResponse>> in_flight_;
    mutable std::mutex in_flight_mutex_;

    // 并发会话上限（P9：与共享线程池线程数对齐）
    static constexpr size_t kMaxConcurrent = 4;
};

} // namespace agent