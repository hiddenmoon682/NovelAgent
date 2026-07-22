#pragma once

// Agent — 核心写小说 Agent，统一处理串行和并行消息处理。
//
// Fix #3: 集成 ExecutionTracer，每个决策步骤自动记录轨迹。
// Fix #6: 集成 StateMachine，状态转换在操作边界自动执行。
//
// Phase 4 线程安全：Agent 通过 LLMClientFactory 创建独立的 LLMClient 实例，
// 不再共享外部引用。每个 Agent 拥有自己的 HTTP 连接状态，确保并行隔离。

#include "agent/AgentOrchestrator.h"
#include "agent/AgentState.h"
#include "agent/ContextManagerTypes.h"
#include "agent/ContextualToolProvider.h"
#include "agent/ExecutionTracer.h"
#include "agent/ToolCallLoop.h"
#include "llm/IMemory.h"
#include "llm/ILLMClient.h"

#include <atomic>
#include <memory>
#include <string>

namespace llm {
class LLMClientFactory;
} // namespace llm

namespace agent {

class ToolRegistry;
class ContextManager;
class TemplateManager;
class ThreadPool;

// Agent 执行约束参数（与 SubAgentConfig 对称）。
struct AgentExecutionConfig {
    int max_tool_rounds = 10;       // 单次用户消息的最大工具调用轮数
    int max_repeated_calls = 3;     // 同一工具调用的最大重复次数
};

class Agent {
public:
    // factory  LLM 客户端工厂（Agent 通过它创建自己的独立 LLMClient）
    // memory   记忆实例（对等组件，由外部组装器注入，Agent 不拥有其生命周期）
    Agent(llm::LLMClientFactory& factory, ToolRegistry& registry, llm::IMemory& memory);
    ~Agent();

    // 设置系统提示词，定义 Agent 的角色、行为和写作风格。
    void setSystemPrompt(std::string prompt);
    // 设置执行约束参数（最大工具调用轮数、重复调用上限）。
    void setExecutionConfig(AgentExecutionConfig config) { exec_config_ = config; }
    // 获取当前执行约束参数。
    const AgentExecutionConfig& executionConfig() const { return exec_config_; }
    // 设置上下文管理器（知识库检索、动态上下文注入）。
    void setContextManager(ContextManager* cm);
    // 获取上下文管理器（非拥有指针，可能为 nullptr）。
    ContextManager* contextManager() const { return context_manager_; }

    // 处理用户输入——核心入口。自动追加对话历史、调用 LLM、执行工具，
    // 并在多轮 tool_call 循环后返回最终 LLMResponse。
    llm::LLMResponse process(const std::string& input,
                              llm::StreamCallbacks callbacks = {});
    // 执行单条命令（单次非工具模式），直接调用 LLM 并返回响应，不维护历史。
    // 无工具调用能力，适用于 REST API 或简单问答场景。
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    // 返回当前记忆（只读），供外部查看或日志记录。
    const llm::IMemory& memory() const { return memory_; }
    // 清空对话历史，开始全新的对话。
    void clearMemory();
    // 重置整个会话（清空对话 + 重置上下文追踪 + 清除压缩摘要）。
    void resetSession();

    // ── 上下文管理便捷方法 ──
    // 执行对话压缩（委托 ContextManager::compact）。
    // 保留最近 ~20 条消息，将其余消息交由 LLM 生成双层摘要（情节事实 + 风格样本）。
    // 摘要存储在 ContextManager 内部，后续 assemble() 自动注入到 system prompt。
    // focus 可选压缩焦点（如"重点关注角色张三的动机变化"）
    agent::CompactResult compactConversation(std::optional<std::string> focus = std::nullopt);
    // 保留指定消息（按 Conversation::all() 索引）。
    // preserved 消息在 truncateMessages 中优先保留但不免 token 预算。
    bool pinMessage(size_t index);
    // 取消保留。
    bool unpinMessage(size_t index);
    // 编辑指定索引的消息内容（仅允许 User 和 Assistant 消息）。
    bool editMessage(size_t index, std::string new_content);
    // 获取上下文统计（累计 token 消耗、请求次数、模型窗口上限）。
    agent::SessionTokenState contextStats() const;
    // 获取最后一次上下文组装的警告列表（截断/用量临界/向量过期/预算溢出等）。
    std::vector<std::string> contextWarnings() const;

    // ── 对话回滚 ──
    // 回滚到指定消息索引（保留 [0, index]），丢弃之后的所有消息。
    // 自动检测是否回滚到压缩点之前 → 清空失效摘要。
    // 同时标记向量库为脏 → 防止检索到"未来"章节片段。
    // 返回 false 表示索引越界。
    bool rewindTo(size_t index);
    // 返回所有 user 消息的索引列表（可作为 /rewind 的回滚点）。
    std::vector<size_t> checkpointIndices() const;

    // ── 会话持久化（灾难恢复）──
    // 保存完整会话状态到 .novelagent/ 目录。
    // conversation.json（对话历史）+ session_meta.json（摘要/token/chapter/preserved/向量脏标记）。
    void saveSessionState();
    // 加载完整会话状态并恢复到 Agent + ContextManager 内部状态。
    // 自动对比 project_mtime：若 Project 在保存后被修改则清空压缩摘要。
    void loadSessionState();

    // ── 处理器策略 ──
    // 使用串行处理器——每轮 tool_call 逐一执行，等待 LLM 返回后再执行下一个。
    void useSerialProcessor() { parallel_mode_ = false; }
    // 使用并行处理器——多轮 tool_call 并发执行，通过编排器协调子 Agent。
    void useParallelProcessor(TemplateManager* templateMgr = nullptr);
    // 当前是否启用了并行处理模式。
    bool isParallelEnabled() const { return parallel_mode_; }

    // ── Fix #3: 可观测性 ──
    // 获取执行轨迹记录器（可变引用），用于在外部记录额外轨迹。
    ExecutionTracer& tracer() { return tracer_; }
    // 获取执行轨迹记录器（只读），供日志或调试输出。
    const ExecutionTracer& tracer() const { return tracer_; }

    // ── Fix #6: 状态查询 ──
    // 返回当前 Agent 状态（Idle / Processing / WaitingTool 等）。
    AgentState currentState() const { return state_.current(); }
    // 返回状态机实例（只读），供外部检查状态转换历史。
    const StateMachine& stateMachine() const { return state_; }
    // 检查 Agent 当前是否可接受新用户输入（仅在 Idle 状态返回 true）。
    bool canAcceptInput() const { return state_.canAcceptInput(); }

    // ── 取消支持 ──

    // 请求取消当前正在进行的处理（线程安全，原子变量写入）。
    // 在 SSE 流式回调中检查该标志，中止 HTTP 连接并返回部分响应。
    void requestCancel() { cancel_requested_.store(true); }
    // 返回取消标志的原子指针（供 ToolCallLoop / SubAgent 使用）。
    std::atomic<bool>* cancelFlag() { return &cancel_requested_; }
    // 重置取消标志（供下一次请求使用）。
    void resetCancel() { cancel_requested_.store(false); }

    // 返回当前 Agent 拥有的 LLMClient（可变引用），供外部直接调用 LLM。
    llm::ILLMClient& client() { return *client_; }
    // 返回工具注册表（可变引用），供外部注册或查询工具。
    ToolRegistry& registry() { return registry_; }
    // 返回渐进式工具上下文提供者。
    ContextualToolProvider& toolContext() { return tool_context_; }

private:
    // 内部处理结果（包含最终文本 + 原始 LLM 响应）。
    struct InternalResult {
        std::string text;
        llm::LLMResponse raw_response;
    };

    // 串行处理路径 — 标准 tool_call 循环。
    InternalResult processSerial(const std::string& input,
                                 llm::IMemory& memory,
                                 llm::StreamCallbacks callbacks);
    // 并行处理路径 — 子任务编排。
    InternalResult processParallel(const std::string& input,
                                   llm::IMemory& memory,
                                   llm::StreamCallbacks callbacks);
    // 构建最终发给 LLM 的系统提示词。
    std::string buildEffectivePrompt(llm::IMemory& memory);

private:

    llm::LLMClientFactory& factory_;                //  LLM 客户端工厂，供并行模式创建子 Agent
    std::unique_ptr<llm::ILLMClient> client_;       //  Agent 自己的独立 LLMClient
    ToolRegistry& registry_;                        //  工具注册表
    llm::IMemory& memory_;                          //  记忆（对等组件，外部注入，非拥有）
    AgentExecutionConfig exec_config_;              //  执行约束参数（轮数/重复上限）
    ContextManager* context_manager_ = nullptr;     //  上下文管理器（非拥有指针）
    bool parallel_mode_ = false;                    //  是否启用并行模式
    std::unique_ptr<AgentOrchestrator> orchestrator_; //  并行编排器
    ContextualToolProvider tool_context_;            //  渐进式工具上下文提供者
    std::unique_ptr<ThreadPool> tool_pool_;          //  工具并发执行线程池

    // Fix #3: 执行轨迹记录器
    ExecutionTracer tracer_;

    // Fix #6: 显式状态机
    StateMachine state_;

    // ── 取消支持 ──
    std::atomic<bool> cancel_requested_{false};   //  外部取消请求（Ctrl+C / SubAgent 超时）
};

} // namespace agent
