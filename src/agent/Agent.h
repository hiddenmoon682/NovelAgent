#pragma once

#include "agent/ToolRegistry.h"
#include "llm/Conversation.h"
#include "llm/LLMClient.h"

#include <string>

namespace agent {

/// 核心 Agent — 接收用户输入，编排 LLM 调用与工具执行。
///
/// 职责：
/// - 维护对话历史（Conversation）
/// - Tool call 循环：LLM 请求工具 → 执行 → 回传结果 → 再次调用 LLM
/// - 单次命令模式（--exec）
///
/// Agent 不拥有 LLMClient 和 ToolRegistry——它们由 main() 管理生命周期，
/// Agent 通过引用持有。
///
/// 使用示例：
///   llm::LLMClient client(config);
///   agent::ToolRegistry registry;
///   agent::Agent agent(client, registry);
///   agent.setSystemPrompt("你是一个网文写作助手。");
///
///   auto response = agent.processUserMessage("帮我写一个开场段落");
class Agent {
public:
    /// @param client    LLM 客户端引用（外部管理生命周期）
    /// @param registry  工具注册中心引用（外部管理生命周期）
    Agent(llm::LLMClient& client, ToolRegistry& registry);

    // ================================================================
    // 配置
    // ================================================================

    /// 设置系统提示词（例如 "你是一个网文写作助手..."）。
    void setSystemPrompt(std::string prompt);

    /// 设置最大 tool call 循环轮数（默认 10）。
    /// 超过此轮数后强制退出循环，返回最近的 LLM 响应。
    void setMaxToolRounds(int n);

    // ================================================================
    // 核心 API
    // ================================================================

    /// 处理用户消息，自动完成 tool call 循环。
    ///
    /// 内部流程：
    ///   1. 将用户消息加入对话历史
    ///   2. 调用 LLM（携带工具定义）
    ///   3. 若 LLM 返回 tool_calls → 执行工具 → 回传结果 → 回到步骤 2
    ///   4. 若 LLM 返回纯文本 → 将回复加入对话历史 → 返回
    ///
    /// @param input      用户输入文本
    /// @param callbacks  流式回调（可选，用于实时显示）
    /// @return           最终 LLMResponse（最后一条 LLM 回复）
    llm::LLMResponse processUserMessage(const std::string& input,
                                         llm::StreamCallbacks callbacks = {});

    /// 单次命令执行（用于 --exec 模式）。
    ///
    /// 与 processUserMessage 的区别：
    /// - 不修改内部 conversation_（适合一次性查询）
    /// - 不触发 tool call 循环（只做一次 LLM 调用）
    /// - 不将结果加入对话历史
    ///
    /// @param command    命令文本
    /// @param callbacks  流式回调（可选）
    /// @return           LLM 响应
    llm::LLMResponse execute(const std::string& command,
                              llm::StreamCallbacks callbacks = {});

    // ================================================================
    // 对话管理
    // ================================================================

    /// 获取当前对话历史（只读）。
    const llm::Conversation& conversation() const { return conversation_; }

    /// 清空对话历史。
    void clearConversation();

private:
    llm::LLMClient& client_;
    ToolRegistry& registry_;
    llm::Conversation conversation_;
    std::string system_prompt_;
    int max_tool_rounds_ = 10;

    /// 调用 LLM + 检查 tool_calls，循环执行直到 LLM 不再请求工具。
    /// @return 最终的 LLM 响应（不含 tool_calls 的那条）
    llm::LLMResponse runToolLoop(llm::StreamCallbacks callbacks);

    /// 构造 Assistant 消息（含 tool_calls 列表）。
    static llm::Message makeAssistantMessage(const llm::LLMResponse& response);

    /// 处理单轮 tool call：执行所有工具调用，将结果加入对话。
    void executeToolCallsAndAppend(const std::vector<llm::ToolCall>& tool_calls);
};

} // namespace agent
