#pragma once

// Agent — 核心写小说 Agent (Agent最佳实践增强版 Fix #3,#6)。
//
// Fix #3: 集成 ExecutionTracer，每个决策步骤自动记录轨迹。
// Fix #6: 集成 StateMachine，状态转换在操作边界自动执行。
//
// Phase 4 线程安全：Agent 通过 LLMClientFactory 创建独立的 LLMClient 实例，
// 不再共享外部引用。每个 Agent 拥有自己的 HTTP 连接状态，确保并行隔离。

#include "agent/AgentState.h"
#include "agent/ContextManagerTypes.h"
#include "agent/ExecutionTracer.h"
#include "agent/IMessageProcessor.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"

#include <memory>
#include <string>

namespace llm {
class LLMClientFactory;
} // namespace llm

namespace agent {

class ToolRegistry;
class ContextManager;
class TemplateManager;

class Agent {
public:
    // @param factory  LLM 客户端工厂（Agent 通过它创建自己的独立 LLMClient）
    Agent(llm::LLMClientFactory& factory, ToolRegistry& registry);
    ~Agent();

    // 设置系统提示词，定义 Agent 的角色、行为和写作风格。
    void setSystemPrompt(std::string prompt);
    // 设置单次用户消息的最大工具调用轮数，防止无限循环。
    void setMaxToolRounds(int n);
    // 设置上下文管理器（知识库检索、动态上下文注入）。
    void setContextManager(ContextManager* cm);
    // 获取上下文管理器（非拥有指针，可能为 nullptr）。
    ContextManager* contextManager() const { return context_manager_; }
    // 设置每次请求的最大上下文 token 数（应用层预算上限），旧消息超出将被截断。
    void setMaxContextTokens(int tokens);

    // 处理用户输入——核心入口。自动追加对话历史、调用 LLM、执行工具，
    // 并在多轮 tool_call 循环后返回最终 LLMResponse。
    llm::LLMResponse processUserMessage(const std::string& input,
                                         llm::StreamCallbacks callbacks = {});
    // 执行单条命令（单次非工具模式），直接调用 LLM 并返回响应，不维护历史。
    //
    // ⚠️ 注意：此方法不使用 ToolCallLoop（无工具调用能力）、不维护 conversation_
    // 对话历史、不触发章节检测/自动 compact/增量保存。与 processUserMessage() 的对比：
    // | 维度 | processUserMessage | execute |
    // |------|-------------------|---------|
    // | 工具循环 | ✅ ToolCallLoop 多轮 | ❌ 直接 chat，无工具 |
    // | 对话历史 | ✅ 维护 conversation_ | ❌ 每次全新临时消息 |
    // | 状态机 | ✅ Thinking→AwaitingTool→Idle | ⚠️ 仅 Thinking→Idle |
    // | 异常恢复 | ✅ B8 try-catch | ✅ Issue 24 已补 |
    // | 上下文注入 | ✅ assemble 完整注入 | ⚠️ 手动简单拼接 |
    //
    // 适用场景：REST API /api/execute（外部脚本单次查询）、不需要工具链的简单问答。
    // 如需多轮对话或工具调用，请使用 processUserMessage()。
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    // 返回当前对话历史（只读），供外部查看或日志记录。
    const llm::Conversation& conversation() const { return conversation_; }
    // 清空对话历史，开始全新的对话。
    void clearConversation();
    // 重置整个会话（清空对话 + 重置上下文追踪 + 清除压缩摘要）。
    void resetSession();

    // ── 上下文管理便捷方法 ──
    // 执行对话压缩（委托 ContextManager::compact）。
    // 保留最近 ~20 条消息，将其余消息交由 LLM 生成双层摘要（情节事实 + 风格样本）。
    // 摘要存储在 ContextManager 内部，后续 assemble() 自动注入到 system prompt。
    // @param focus 可选压缩焦点（如"重点关注角色张三的动机变化"）
    agent::CompactResult compactConversation(std::optional<std::string> focus = std::nullopt);
    // 保留指定消息（按 Conversation::all() 索引）。
    // preserved 消息在 truncateMessages 中优先保留但不免 token 预算。
    bool pinMessage(size_t index);
    // 取消保留。
    bool unpinMessage(size_t index);
    // 获取上下文统计（累计 token 消耗、请求次数、模型窗口上限）。
    agent::SessionTokenState contextStats() const;
    // 获取最后一次上下文组装的警告列表（截断/用量临界/向量过期/预算溢出等）。
    std::vector<std::string> contextWarnings() const;
    // 设置当前活跃章节（供章节边界检测使用）。
    void setCurrentChapter(const std::string& chapter_id);

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
    void useSerialProcessor();
    // 使用并行处理器——多轮 tool_call 并发执行，通过编排器协调子 Agent。
    void useParallelProcessor(TemplateManager* templateMgr = nullptr);
    // 自定义消息处理器，覆盖默认策略。
    void setProcessor(std::unique_ptr<IMessageProcessor> processor);
    // 当前是否启用了并行处理器模式。
    bool isParallelEnabled() const;

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

    // 返回当前 Agent 拥有的 LLMClient（可变引用），供外部直接调用 LLM。
    llm::ILLMClient& client() { return *client_; }
    // 返回工具注册表（可变引用），供外部注册或查询工具。
    ToolRegistry& registry() { return registry_; }

private:
    llm::LLMClientFactory& factory_;                //  LLM 客户端工厂，供 useParallelProcessor 创建子 Agent 时传递给编排器
    std::unique_ptr<llm::ILLMClient> client_;       //  Agent 自己的独立 LLMClient，通过 factory_ 创建，确保 HTTP 连接隔离
    ToolRegistry& registry_;                        //  工具注册表，维护所有可用工具的定义和查找
    llm::Conversation conversation_;                //  对话历史（Message 列表），每次 processUserMessage 自动追加
    std::string system_prompt_;                     //  系统提示词，设定 Agent 行为边界和写作风格
    int max_tool_rounds_ = 10;                      //  单次用户消息的最大工具调用轮数，防止无限循环
    ContextManager* context_manager_ = nullptr;     //  上下文管理器（非拥有指针），管理知识库检索和动态上下文注入
    int max_context_tokens_ = 131072;               //  每次请求的最大上下文 token 数（应用层预算上限），用于截断对话历史
    std::unique_ptr<IMessageProcessor> processor_;  //  消息处理策略（串行/并行），决定多轮 tool_call 的执行模式

    // Fix #3: 执行轨迹记录器 — 自动记录每次 LLM 调用、工具执行和状态转换的详细轨迹
    ExecutionTracer tracer_;

    // Fix #6: 显式状态机 — 管理 Agent 生命周期状态（Idle/Processing/WaitingTool 等）
    StateMachine state_;

    // ── 章节边界检测 ──
    std::string last_chapter_id_;               //  上一轮检测到的活跃章节 ID
    // 章节边界自动 compact。
    //
    // 在 LLM 响应返回后检查 tool_calls 中是否包含 create_chapter 或 read_chapter，
    // 提取 chapter_id 并与 last_chapter_id_ 对比。若检测到章节切换：
    //   1. 对旧章节对话执行 compact（保留关键情节决策 + 写作风格）
    //   2. 更新 last_chapter_id_ 和 ContextManager::current_chapter_id_
    //
    // 首次进入章节时（last_chapter_id_ 为空）不触发 compact。
    void maybeAutoCompact(const llm::LLMResponse& response);
};

} // namespace agent
