#pragma once

/// 消息处理器抽象接口 — 解耦 Agent 的串行/并行/规划模式分发。
///
/// 架构改进（P1）：消除 Agent::processUserMessage() 中的硬编码 if-else 分支。
/// 新增处理模式（如"先规划再执行"）只需实现此接口并注入，不需修改 Agent。

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

/// 消息处理器抽象接口。
class IMessageProcessor {
public:
    virtual ~IMessageProcessor() = default;

    /// Fix #3: 运行时更新 system prompt（避免 Agent::setSystemPrompt 的 dynamic_cast）。
    virtual void setSystemPrompt(const std::string& prompt) = 0;

    /// 处理用户消息，返回 LLM 的最终回复文本。
    struct Result {
        std::string text;
        llm::LLMResponse raw_response;
    };
    virtual Result process(
        const std::string& input,
        llm::Conversation& conversation,
        llm::StreamCallbacks callbacks) = 0;
};

/// 串行处理器 — 标准 tool call 循环模式（默认）。
/// 使用父 Agent 拥有的 LLMClient 引用，自身不持有所有权。
class SerialProcessor : public IMessageProcessor {
public:
    /// @param client  父 Agent 的 LLMClient 引用（SerialProcessor 不持有所有权）
    SerialProcessor(llm::ILLMClient& client, ToolRegistry& registry,
                    std::string system_prompt);

    Result process(const std::string& input,
                   llm::Conversation& conversation,
                   llm::StreamCallbacks callbacks) override;

    /// 设置上下文管理器（可选）。
    void setContextManager(class ContextManager* cm) { context_manager_ = cm; }
    void setContextWindow(int window) { context_window_ = window; }
    void setMaxToolRounds(int n) { max_tool_rounds_ = n; }
    void setTracer(class ExecutionTracer* t) { tracer_ = t; }

    // Fix #3: 实现接口
    void setSystemPrompt(const std::string& p) override { system_prompt_ = p; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    std::string system_prompt_;
    class ContextManager* context_manager_ = nullptr;
    int context_window_ = 65536;
    int max_tool_rounds_ = 10;
    class ExecutionTracer* tracer_ = nullptr;  // Fix #3

    std::string buildEffectivePrompt(
        const llm::Conversation& conversation,
        std::vector<llm::Message>& out_messages);
};

/// 并行处理器 — 委托 AgentOrchestrator 做并行编排。
class ParallelProcessor : public IMessageProcessor {
public:
    /// @param factory  LLM 客户端工厂（传递给 AgentOrchestrator 用于创建独立客户端）
    ParallelProcessor(llm::LLMClientFactory& factory, ToolRegistry& registry,
                      std::string system_prompt);
    ~ParallelProcessor() override;

    Result process(const std::string& input,
                   llm::Conversation& conversation,
                   llm::StreamCallbacks callbacks) override;
    void setSystemPrompt(const std::string& p) override;

    AgentOrchestrator& orchestrator() { return *orchestrator_; }
    void setTemplateManager(class TemplateManager* tm);

private:
    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;
    std::unique_ptr<AgentOrchestrator> orchestrator_;
    std::string system_prompt_;
};

} // namespace agent
