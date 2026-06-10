#pragma once

/// Agent — 核心写小说 Agent。
///
/// P1 架构改进：
/// - 通过 IMessageProcessor 策略接口支持串行/并行/规划模式
/// - 新增处理模式无需修改 Agent，只需实现 IMessageProcessor 并注入
///
/// 职责：
/// - 维护对话历史（Conversation）
/// - 通过可插拔处理器分发用户消息

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
    /// @param client    LLM 客户端引用（外部管理生命周期）
    /// @param registry  工具注册中心引用（外部管理生命周期）
    Agent(llm::ILLMClient& client, ToolRegistry& registry);
    ~Agent();

    // ================================================================
    // 配置
    // ================================================================

    void setSystemPrompt(std::string prompt);
    void setMaxToolRounds(int n);

    void setContextManager(ContextManager* cm);
    void setContextWindow(int window);

    // ================================================================
    // 核心 API
    // ================================================================

    /// 处理用户消息（通过当前注入的 IMessageProcessor）。
    llm::LLMResponse processUserMessage(const std::string& input,
                                         llm::StreamCallbacks callbacks = {});

    /// 单次命令执行（--exec 模式）。
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    // ================================================================
    // 对话管理
    // ================================================================

    const llm::Conversation& conversation() const { return conversation_; }
    void clearConversation();

    // ================================================================
    // 处理器策略（P1）
    // ================================================================

    /// 切换到串行处理器（默认）。
    void useSerialProcessor();

    /// 切换到并行处理器（委托 AgentOrchestrator）。
    void useParallelProcessor(TemplateManager* templateMgr = nullptr);

    /// 注入自定义处理器（如 PlanThenExecuteProcessor）。
    void setProcessor(std::unique_ptr<IMessageProcessor> processor);

    /// 是否启用了并行编排。
    bool isParallelEnabled() const;

    /// 获取底层 LLM 客户端。
    llm::ILLMClient& client() { return client_; }

    /// 获取工具注册中心。
    ToolRegistry& registry() { return registry_; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    llm::Conversation conversation_;
    std::string system_prompt_;
    int max_tool_rounds_ = 10;
    ContextManager* context_manager_ = nullptr;
    int context_window_ = 65536;

    // P1: 可插拔消息处理器
    std::unique_ptr<IMessageProcessor> processor_;
};

} // namespace agent
