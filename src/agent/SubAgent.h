#pragma once

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"

#include <chrono>
#include <string>
#include <vector>

namespace agent {

class ToolRegistry;

/// 子 Agent — 独立对话上下文 + 受限工具集 + 超时保护。
///
/// 主 Agent 通过 AgentOrchestrator 并行派发多个 SubAgent，
/// 每个 SubAgent 独立执行子任务，结果汇总后返回主 Agent。
///
/// 安全约束：
/// - 独立 `Conversation`（不与主 Agent 共享）
/// - 受限工具集（默认只读，通过 allowedTools 控制）
/// - 超时保护（默认 120s，防止单个子 Agent 卡死）
struct SubAgentConfig {
    std::string task;                        // 子任务描述
    std::string system_prompt;               // 专用 system prompt
    std::vector<std::string> allowed_tools;  // 允许使用的工具名列表（空=无工具）
    std::chrono::seconds timeout{120};
    int max_tool_rounds = 3;                 // 子 Agent 工具循环上限（比主 Agent 少）
};

struct SubAgentResult {
    std::string output;     // 子 Agent 的最终回复文本
    bool timed_out = false;
    std::string error;
};

class SubAgent {
public:
    /// @param client    LLM 客户端（与主 Agent 共享，但独立调用）
    /// @param registry  工具注册中心（子 Agent 只能使用 allowed_tools 中的工具）
    SubAgent(llm::ILLMClient& client, ToolRegistry& registry);

    /// 执行子任务，阻塞直到完成或超时。
    SubAgentResult execute(const SubAgentConfig& config);

    /// 获取子 Agent 的独立对话历史（用于调试）。
    const llm::Conversation& conversation() const { return conversation_; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    llm::Conversation conversation_;

    /// 从 registry 中筛选出 allowed_tools，构造受限工具定义列表。
    std::vector<llm::ToolDefinition> filterTools(
        const std::vector<std::string>& allowed) const;
};

} // namespace agent
