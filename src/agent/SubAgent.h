#pragma once

/// 子 Agent — 独立对话上下文 + 受限工具集 + 超时保护。
///
/// P0 架构改进：
/// - 通过 IToolProvider& 而非 ToolRegistry& 访问工具
/// - RestrictedToolProvider 在类型系统层面保证安全约束
/// - 使用 ToolCallLoop 复用 tool call 循环引擎

#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace agent {

struct SubAgentConfig {
    std::string task;
    std::string system_prompt;
    std::vector<std::string> allowed_tools;
    std::chrono::seconds timeout{120};
    int max_tool_rounds = 3;
};

struct SubAgentResult {
    std::string output;
    bool timed_out = false;
    std::string error;
};

class SubAgent {
public:
    /// @param client    LLM 客户端（与主 Agent 共享）
    /// @param tools     工具提供者（受限视图，只能调用白名单工具）
    SubAgent(llm::ILLMClient& client, IToolProvider& tools);

    /// 执行子任务，阻塞直到完成或超时。
    SubAgentResult execute(const SubAgentConfig& config);

    const llm::Conversation& conversation() const { return conversation_; }

private:
    llm::ILLMClient& client_;
    IToolProvider& tools_;
    llm::Conversation conversation_;
    std::mutex conv_mutex_;               // 保护 conversation_ 并发访问
    std::atomic<bool> cancelled_{false};  // 超时时通知异步任务停止
};

} // namespace agent
