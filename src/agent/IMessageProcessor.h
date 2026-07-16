#pragma once

// 消息处理器抽象接口 — 解耦 Agent 的串行/并行/规划模式分发。
//
// 架构改进（P1）：消除 Agent::processUserMessage() 中的硬编码 if-else 分支。
// 新增处理模式（如"先规划再执行"）只需实现此接口并注入，不需修改 Agent。

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"

#include <memory>
#include <string>

namespace llm {
class LLMClientFactory;
} // namespace llm

namespace agent {

class ToolCallLoop;
class ToolRegistry;
class AgentOrchestrator;

// 消息处理器抽象接口。
class IMessageProcessor {
public:
    virtual ~IMessageProcessor() = default;

    // Fix #3: 运行时更新 system prompt（避免 Agent::setSystemPrompt 的 dynamic_cast）。
    virtual void setSystemPrompt(const std::string& prompt) = 0;

    // 统一配置接口 — 消除 Agent 中的 dynamic_cast 向下转型。
    // 新增 Processor 类型只需 override 相关方法，不需修改 Agent 代码。
    // 默认实现为空操作，子类按需覆盖。

    // 设置上下文管理器（可选）。
    virtual void setContextManager(class ContextManager* /*cm*/) {}
    // 设置每次请求的最大上下文 token 数。
    virtual void setMaxContextTokens(int /*tokens*/) {}
    // 设置单次请求的最大工具调用轮数。
    virtual void setMaxToolRounds(int /*n*/) {}
    // 设置执行轨迹记录器。
    virtual void setTracer(class ExecutionTracer* /*t*/) {}
    // 设置状态机。
    virtual void setStateMachine(class StateMachine* /*s*/) {}

    // 处理用户消息，返回 LLM 的最终回复文本。
    struct Result {
        std::string text;
        llm::LLMResponse raw_response;
    };
    virtual Result process(
        const std::string& input,
        llm::Conversation& conversation,
        llm::StreamCallbacks callbacks) = 0;

    // B1: 判断是否为并行处理器（替代 dynamic_cast）。
    virtual bool isParallel() const { return false; }
};

// 串行处理器 — 标准 tool call 循环模式（默认）。
// 使用父 Agent 拥有的 LLMClient 引用，自身不持有所有权。
class SerialProcessor : public IMessageProcessor {
public:
    // client  父 Agent 的 LLMClient 引用（SerialProcessor 不持有所有权）
    SerialProcessor(llm::ILLMClient& client, ToolRegistry& registry,
                    std::string system_prompt);

    Result process(const std::string& input,
                   llm::Conversation& conversation,
                   llm::StreamCallbacks callbacks) override;

    // 设置上下文管理器（可选）。
    void setContextManager(class ContextManager* cm) override { context_manager_ = cm; }
    void setMaxContextTokens(int tokens) override { max_context_tokens_ = tokens; }
    void setMaxToolRounds(int n) override { max_tool_rounds_ = n; }
    void setTracer(class ExecutionTracer* t) override { tracer_ = t; }
    void setStateMachine(class StateMachine* s) override { state_ = s; }  // D1.1

    // Fix #3: 实现接口
    void setSystemPrompt(const std::string& p) override { system_prompt_ = p; }

private:
    // LLM 客户端引用（由父 Agent 创建，本对象不持有所有权）。
    // 负责所有与 LLM API 的通信（chat/completions 接口）。
    llm::ILLMClient& client_;

    // 工具注册表引用（由父 Agent 注入）。
    // 包含所有已注册的工具定义及其实现；tool_call 循环从中查找并执行工具。
    ToolRegistry& registry_;

    // 系统提示词 — 设定 LLM 的角色和行为准则。
    // 在 process() 中会和 ContextManager 提供的动态上下文拼接成最终版 system prompt。
    std::string system_prompt_;

    // 上下文管理器（可选指针，允许为 nullptr）。
    // 负责构建动态 system prompt（项目上下文）、Token 追踪、对话压缩、会话持久化。
    // 为 buildEffectivePrompt() 提供额外的系统级上下文。
    class ContextManager* context_manager_ = nullptr;

    // 每次请求的最大上下文 token 数（应用层预算上限），默认 131072（128K）。
    // 用于 ContextManager 做消息裁剪，避免超出用户设定的成本上限。
    int max_context_tokens_ = 131072;

    // 单轮用户请求的最大 tool_call 轮数，默认 10 轮。
    // 防止 LLM 陷入无限 tool_call 循环；达到上限后强制返回已有结果。
    int max_tool_rounds_ = 10;

    // 执行轨迹记录器（可选指针，允许为 nullptr）。
    // 记录每次 process() 的执行耗时、token 消耗等指标，用于性能监控和调试。
    class ExecutionTracer* tracer_ = nullptr;  // Fix #3
    class StateMachine* state_ = nullptr;       // D1.1

    // 构建最终发给 LLM 的系统提示词。
    // 将固定 system_prompt_ 与 ContextManager 提供的动态上下文拼接，
    // 同时将处理后的消息列表填充到 out_messages 中一并发送。
    // conversation 非 const — assemble() 可能在自动压缩时修改对话（删除旧消息、插入摘要）。
    std::string buildEffectivePrompt(
        llm::Conversation& conversation,
        std::vector<llm::Message>& out_messages);
};

// 并行处理器 — 委托 AgentOrchestrator 做并行编排。
class ParallelProcessor : public IMessageProcessor {
public:
    // factory  LLM 客户端工厂（传递给 AgentOrchestrator 用于创建独立客户端）
    ParallelProcessor(llm::LLMClientFactory& factory, ToolRegistry& registry,
                      std::string system_prompt);
    ~ParallelProcessor() override;

    Result process(const std::string& input,
                   llm::Conversation& conversation,
                   llm::StreamCallbacks callbacks) override;
    void setSystemPrompt(const std::string& p) override;

    // 统一配置接口 — 与 SerialProcessor 对齐（Issue 22+25 修复）
    void setContextManager(class ContextManager* cm) override { context_manager_ = cm; }
    void setMaxContextTokens(int tokens) override { max_context_tokens_ = tokens; }
    void setMaxToolRounds(int n) override { max_tool_rounds_ = n; }
    void setTracer(class ExecutionTracer* t) override { tracer_ = t; }
    void setStateMachine(class StateMachine* s) override { state_ = s; }

    bool isParallel() const override { return true; }

    AgentOrchestrator& orchestrator() { return *orchestrator_; }
    void setTemplateManager(class TemplateManager* tm);

private:
    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;
    std::unique_ptr<AgentOrchestrator> orchestrator_;
    std::string system_prompt_;
    class ContextManager* context_manager_ = nullptr;  // A18.3
    int max_context_tokens_ = 131072;                  // Issue 22: 与 SerialProcessor 对齐
    int max_tool_rounds_ = 10;                         // Issue 22: 与 SerialProcessor 对齐
    class ExecutionTracer* tracer_ = nullptr;           // Issue 25: 与 SerialProcessor 对齐
    class StateMachine* state_ = nullptr;               // Issue 25: 与 SerialProcessor 对齐
};

} // namespace agent
