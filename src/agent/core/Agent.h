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

namespace agent {

// Agent 执行配置（工具循环限制与加载策略）。
struct AgentExecutionConfig {
    int max_tool_rounds = 10;              // 单次 process 的最大工具调用轮数
    int max_repeated_calls = 3;            // 同一工具+参数的最大重复次数（防死循环）
    bool progressive_tool_loading = true;  // 渐进式工具加载开关（false = 全量暴露）
};

// 上下文用量快照（供 UI 展示，refreshUsage() 刷新）。
struct ContextUsage {
    int total_tokens = 0;  // 当前上下文总 token 数（含 system prompt）
    int percent = 0;       // 占模型上限的百分比（0~100）
};

// Agent — LLM 编排门面：驱动“用户输入 → CoreLoop（LLM↔工具）→ 回复”
// 主流程，并负责上下文预算评估/压缩、状态机、取消与执行轨迹；
// 会话生命周期与消息级操作薄转发给 SessionManager。
class Agent {
public:
    // @param factory LLM 客户端工厂；非拥有引用，调用方保证存活期覆盖 Agent。
    // @param registry 工具注册中心；非拥有引用，存活期约定同上。
    // @param memory 共享记忆；非拥有引用，存活期约定同上。
    Agent(llm::LLMClientFactory& factory, ToolRegistry& registry, llm::IMemory& memory);
    ~Agent();

    // 设置 system prompt（写入共享 memory，替换旧值）。
    void setSystemPrompt(std::string prompt);
    // 返回延迟工具存根（静态文本，供 setup 时注入 system prompt）。
    std::string deferredToolsStub() const { return progressive_tools_.deferredToolsStub(); }
    // 设置/读取执行配置（工具轮数上限等）。
    void setExecutionConfig(AgentExecutionConfig config) { exec_config_ = config; }
    const AgentExecutionConfig& executionConfig() const { return exec_config_; }

    // 注入模型上下文上限（token）并刷新用量快照。
    // 相比注入整个 TokenBudget，装配层只需提供真实模型上限值，无需了解预算结构；
    // 其余阈值（warning/critical）沿用 TokenBudget 默认值。
    void setModelLimit(int limit) {
        budget_.model_limit = limit;
        refreshUsage();  // 预算变化后百分比需重算（启动时也借此建立初始用量）
    }
    const TokenBudget& tokenBudget() const { return budget_; }
    // 注入 Token 校准器（非拥有指针，可选；调用方保证存活期）。
    void setCalibrator(llm::TokenCounter* cal) { calibrator_ = cal; }
    // 注入会话持久化（非拥有指针，可选；转发给 SessionManager）。
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

    // 处理一轮用户输入：校验 → CoreLoop 多轮工具循环 → 会话落盘 → 刷新用量。
    // 异常时回滚 memory 到调用前快照并恢复状态机（不抛出）。
    // @param input 用户输入；空串或校验失败时不调用 LLM。
    // @param callbacks 流式回调（token 增量/工具事件等），可为空。
    // @return LLM 最终响应；失败时 finish_reason 标记原因
    //         （empty_input / invalid_input / state_rejected / error）。
    llm::LLMResponse process(const std::string& input,
                              llm::StreamCallbacks callbacks = {});
    // 单次命令执行：不携带对话历史（仅 system prompt + 本条命令），
    // 单次 chat 调用无工具循环，不写入 memory、不落盘会话。
    // @return LLM 响应；校验失败/异常时 finish_reason 为 invalid_input / error。
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    // 共享记忆只读访问。
    const llm::IMemory& memory() const { return memory_; }
    // 清空全部对话消息与 system prompt。
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

    // 手动触发对话压缩：较旧消息交给 LLM 生成摘要并替换，应用后触发
    // 摘要沉淀回调（若已注入 setSummarySink）。
    // @param focus 可选聚焦方向（如场景/角色），引导摘要侧重点。
    // @return 压缩统计；messages_compacted 为 0 表示未达压缩条件、未应用。
    CompactionResult compactConversation(std::optional<std::string> focus = std::nullopt);
    // 消息保留标记与编辑（转发 SessionManager；index 为 all() 视角索引）。
    bool pinMessage(size_t index) { return session_manager_.pinMessage(index); }
    bool unpinMessage(size_t index) { return session_manager_.unpinMessage(index); }
    bool editMessage(size_t index, std::string new_content) {
        return session_manager_.editMessage(index, std::move(new_content));
    }
    // 最近一次预算评估产生的警告列表（供 UI 展示）。
    std::vector<std::string> contextWarnings() const { return last_warnings_; }

    // ── 对话回滚（转发 SessionManager，语义见其声明）──
    bool rewindTo(size_t index) { return session_manager_.rewindTo(index); }
    std::vector<size_t> checkpointIndices() const { return session_manager_.checkpointIndices(); }

    // ── 会话持久化（转发 SessionManager，语义见其声明）──
    void saveSessionState() { session_manager_.saveSessionState(); }
    void loadSessionState() { session_manager_.loadSessionState(); }

    // ── 上下文用量（UI 展示）──

    // 最近一次刷新的上下文用量快照。
    ContextUsage contextUsage() const { return usage_; }

    // ── 可观测性 ──
    ExecutionTracer& tracer() { return tracer_; }
    const ExecutionTracer& tracer() const { return tracer_; }

    // ── 状态查询 ──
    AgentState currentState() const { return state_.current(); }
    const StateMachine& stateMachine() const { return state_; }
    bool canAcceptInput() const { return state_.canAcceptInput(); }

    // ── 取消支持 ──

    // 请求取消当前处理（CoreLoop 在轮次边界检查并优雅终止）。
    void requestCancel() { cancel_requested_.store(true); }
    // 取消标志指针（Agent 拥有，随 Agent 存活；供外部监听/置位）。
    std::atomic<bool>* cancelFlag() { return &cancel_requested_; }
    // 清除取消标志（每次 process 开始时自动调用）。
    void resetCancel() { cancel_requested_.store(false); }

    // ── 底层组件访问器（供装配/测试使用）──
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
