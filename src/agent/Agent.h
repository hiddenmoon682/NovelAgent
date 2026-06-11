#pragma once

/// Agent — 核心写小说 Agent (Agent最佳实践增强版 Fix #3,#6)。
///
/// Fix #3: 集成 ExecutionTracer，每个决策步骤自动记录轨迹。
/// Fix #6: 集成 StateMachine，状态转换在操作边界自动执行。

#include "agent/AgentState.h"
#include "agent/ExecutionTracer.h"
#include "agent/IMessageProcessor.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"

#include <memory>
#include <string>

namespace agent {

class ToolRegistry;
class ContextManager;
class TemplateManager;

class Agent {
public:
    Agent(llm::ILLMClient& client, ToolRegistry& registry);
    ~Agent();

    void setSystemPrompt(std::string prompt);
    void setMaxToolRounds(int n);
    void setContextManager(ContextManager* cm);
    void setContextWindow(int window);

    llm::LLMResponse processUserMessage(const std::string& input,
                                         llm::StreamCallbacks callbacks = {});
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    const llm::Conversation& conversation() const { return conversation_; }
    void clearConversation();

    // ── 处理器策略 ──
    void useSerialProcessor();
    void useParallelProcessor(TemplateManager* templateMgr = nullptr);
    void setProcessor(std::unique_ptr<IMessageProcessor> processor);
    bool isParallelEnabled() const;

    // ── Fix #3: 可观测性 ──
    ExecutionTracer& tracer() { return tracer_; }
    const ExecutionTracer& tracer() const { return tracer_; }

    // ── Fix #6: 状态查询 ──
    AgentState currentState() const { return state_.current(); }
    const StateMachine& stateMachine() const { return state_; }
    bool canAcceptInput() const { return state_.canAcceptInput(); }

    llm::ILLMClient& client() { return client_; }
    ToolRegistry& registry() { return registry_; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    llm::Conversation conversation_;
    std::string system_prompt_;
    int max_tool_rounds_ = 10;
    ContextManager* context_manager_ = nullptr;
    int context_window_ = 65536;
    std::unique_ptr<IMessageProcessor> processor_;

    // Fix #3: 执行轨迹记录器
    ExecutionTracer tracer_;

    // Fix #6: 显式状态机
    StateMachine state_;
};

} // namespace agent
