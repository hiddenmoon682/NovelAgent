#pragma once

// SessionRuntime — 每会话一个的独立运行时（多会话并行架构核心）。
//
// 承载单个会话的全部运行时状态（memory/client/state/tools/pipeline/tracer/usage/cancel），
// 与全局共享资源（factory/registry/calibrator/budget 组件）分离。多个会话的 process 可同时
// 执行、互不干扰，使「后台会话继续跑」成为可能。
//
// 线程约束：单个 SessionRuntime 的 process 只被一个工作线程调用（共享线程池调度），
// 故其内部状态无需加锁；跨会话共享资源由各自内部锁保护。

#include "agent/core/AgentState.h"
#include "agent/context/ContextBudgetEvaluator.h"
#include "agent/context/Compactor.h"
#include "agent/context/TokenBudget.h"
#include "agent/context/Memory.h"
#include "agent/tool/ProgressiveToolProvider.h"
#include "agent/tool/ToolPipeline.h"
#include "agent/core/ExecutionTracer.h"
#include "llm/ILLMClient.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace llm {
class LLMClientFactory;
class TokenCounter;
} // namespace llm

namespace agent {

class ToolRegistry;
class SessionPersistence;

// 上下文用量快照（供 UI 展示，refreshUsage() 刷新）。
struct ContextUsage {
    int total_tokens = 0;  // 当前上下文总 token 数（含 system prompt）
    int percent = 0;       // 占模型上限的百分比（0~100）
};

// Agent 执行配置（工具循环限制与加载策略）。
struct AgentExecutionConfig {
    int max_tool_rounds = 10;              // 单次 process 的最大工具调用轮数
    int max_repeated_calls = 3;            // 同一工具+参数的最大重复次数（防死循环）
    bool progressive_tool_loading = true;  // 渐进式工具加载开关（false = 全量暴露）
};

// 会话装配配置（共享源，D11/P5）：SessionRuntime 创建时从共享源拷贝注入。
struct SessionConfig {
    std::function<std::string()> system_prompt_provider;  // 会话边界重建 prompt
    int model_limit = 0;                                  // 模型上下文上限
    std::function<void(const std::string&)> summary_sink; // 压缩摘要沉淀回调
};

// SessionRuntime 核心依赖（P5：构造参数一次性注入；相对固定、生命周期内不变）。
// client 例外：可单独懒重建（D6 空闲休眠），不在此结构内。
struct SessionRuntimeDeps {
    SessionPersistence* persistence = nullptr;      // 持久化（按 id 落盘）
    llm::TokenCounter* calibrator = nullptr;        // 共享校准器
    AgentExecutionConfig exec_config;               // 工具循环执行配置
    SessionConfig config;                           // 共享装配配置（summary_sink 等）
    std::string system_prompt;                      // 创建时注入的 system prompt（D11）
    int model_limit = 0;                            // 模型上下文上限
};

// 每会话独立运行时。构造时从共享源创建独立 client 与工具管道。
class SessionRuntime {
public:
    SessionRuntime(const std::string& session_id,
                   llm::LLMClientFactory& factory,
                   ToolRegistry& registry,
                   SessionRuntimeDeps deps);

    // 会话 id。
    const std::string& sessionId() const { return session_id_; }
    // 上下文用量快照（供 UI 展示，refreshUsage() 刷新）。跨线程：池线程写、GUI 线程读 → 加锁。
    ContextUsage contextUsage() const {
        std::lock_guard<std::mutex> lk(usage_mutex_);
        return usage_;
    }

    // 处理一轮用户输入（作用于本会话）。
    llm::LLMResponse process(const std::string& input, llm::StreamCallbacks callbacks = {});

    // ── 消息级操作（收敛后：移入 runtime，操作自身内存）──

    // 标记/取消标记消息为保留（pin；index 为 all() 视角索引）。
    bool pinMessage(size_t index) { return memory_.pin(index); }
    bool unpinMessage(size_t index) { return memory_.unpin(index); }
    // 编辑消息内容（仅限 User/Assistant 消息，编辑后清除 pin 标记）。
    bool editMessage(size_t index, std::string new_content) {
        return memory_.edit(index, std::move(new_content));
    }
    // 回滚到指定消息（保留 [0, index]；index 越界返回 false）。
    bool rewindTo(size_t index);
    // 全部用户消息的索引（all() 视角），作为可回滚检查点。
    std::vector<size_t> checkpointIndices() const;
    // 手动压缩本会话（较旧消息 LLM 生成摘要并替换）。
    CompactionResult compactConversation(std::optional<std::string> focus = std::nullopt);

    // 请求取消本会话（置 cancel_requested_）。
    void requestCancel() { cancel_requested_.store(true); }
    // 清除取消标志（每次 process 开始时自动调用）。
    void resetCancel() { cancel_requested_.store(false); }
    bool cancelled() const { return cancel_requested_.load(); }

    // 请求删除本会话（独立于 cancel_requested_）：删除运行中会话时置位，process 开头检查
    // 立即退出，且不被 process 开头的 resetCancel() 清零——解决排队任务启动时取消被吞的竞态。
    void setDeleteRequested() { delete_requested_.store(true); }
    void clearDeleteRequested() { delete_requested_.store(false); }
    bool deleteRequested() const { return delete_requested_.load(); }

    // 共享记忆只读访问（当前会话的对话上下文）。
    llm::Memory& memory() { return memory_; }
    const llm::Memory& memory() const { return memory_; }
    // 清空本会话全部对话消息与 system prompt。
    void clearMemory() { memory_.clear(); }
    // 延迟工具存根（静态文本，供装配期注入 system prompt）。
    std::string deferredToolsStub() const { return progressive_tools_.deferredToolsStub(); }
    // 状态查询。
    AgentState currentState() const { return state_.current(); }
    const StateMachine& stateMachine() const { return state_; }
    bool canAcceptInput() const { return state_.canAcceptInput(); }
    // 可观测性。
    ExecutionTracer& tracer() { return tracer_; }
    const ExecutionTracer& tracer() const { return tracer_; }
    // 底层组件访问（client 休眠时懒重建）。
    llm::ILLMClient& client();
    ProgressiveToolProvider& progressiveTools() { return progressive_tools_; }
    // 会话是否已落盘（方案 C：id 提前生成、文件延迟落盘）。
    bool persisted() const { return persisted_.load(); }
    void setPersisted(bool v) { persisted_.store(v); }
    // 运行状态（供调用方判断是否空闲可休眠）。跨线程：池线程写、GUI 线程读。
    bool running() const { return running_.load(); }
    // 释放 client 连接（D6 空闲休眠）：切走/空闲时释放 HTTP 连接，下次 process 懒重建。
    void releaseClient() { client_.reset(); }
    // client 是否已释放（休眠中）。
    bool clientReleased() const { return !client_; }

    // 全量保存本会话到 <session_id>.json（D3，按 id 隔离）。
    void saveSessionState();
    // 从 <session_id>.json 恢复本会话消息（保留当前 system prompt）。
    void loadSessionState();

    // 最近一次预算评估产生的警告列表（供 UI 展示）。
    std::vector<std::string> contextWarnings() const { return last_warnings_; }

private:
    struct InternalResult {
        std::string text;
        llm::LLMResponse raw_response;
    };

    InternalResult processSerial(const std::string& input, llm::StreamCallbacks callbacks);
    void applyCompaction(const CompactionResult& cr);
    void refreshUsage();

    std::string session_id_;
    // 会话是否已落盘（方案 C：延迟落盘）。池线程写、GUI 线程读 → 原子。
    std::atomic<bool> persisted_{false};
    // 运行状态（池线程写、GUI 线程读）→ 原子。
    std::atomic<bool> running_{false};

    // 每会话独有运行时状态
    llm::Memory memory_;
    std::unique_ptr<llm::ILLMClient> client_;
    ExecutionTracer tracer_;
    StateMachine state_;
    ProgressiveToolProvider progressive_tools_;
    ToolPipeline pipeline_;
    ContextUsage usage_;
    mutable std::mutex usage_mutex_;  // 保护 usage_（refreshUsage 写 / contextUsage 读）
    std::atomic<bool> cancel_requested_{false};
    // 删除请求标志（独立于取消标志；删除运行中会话时置位，process 开头检查）
    std::atomic<bool> delete_requested_{false};
    AgentExecutionConfig exec_config_;

    // 共享引用（非拥有）
    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;
    llm::TokenCounter* calibrator_ = nullptr;
    SessionPersistence* persistence_ = nullptr;

    // 上下文管理
    ContextBudgetEvaluator budget_evaluator_;
    Compactor compactor_;
    TokenBudget budget_;
    std::vector<std::string> last_warnings_;

    // 装配配置
    SessionConfig config_;
};

} // namespace agent