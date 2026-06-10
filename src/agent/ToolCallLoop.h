#pragma once

/// Tool Call 循环引擎 — Agent 和 SubAgent 共享的核心算法。
///
/// 架构改进（P0）：消除 Agent::runToolLoop() 与 SubAgent::execute() 中 ~90 行重复代码。
/// 统一首轮调用→检查tool_calls→执行→回传→循环 的标准流程。
///
/// 使用示例:
///   ToolCallLoop loop(client, registry);
///   auto result = loop.run(conversation, tools, system_prompt, callbacks, config);

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"

#include <chrono>
#include <string>
#include <vector>

namespace agent {

class ToolRegistry;

/// Tool Call 循环配置。
struct ToolCallLoopConfig {
    int max_rounds = 10;                     // 最大循环轮数
    bool first_round_streaming = true;       // 首轮是否流式
    std::chrono::seconds timeout{0};         // 超时（0=无超时）
};

/// Tool Call 循环结果。
struct ToolCallLoopResult {
    llm::LLMResponse response;               // 最终 LLM 响应
    bool timed_out = false;                  // 是否超时
    std::string error;                       // 错误消息
    int rounds_executed = 0;                 // 实际执行的轮数
};

/// Tool Call 循环引擎 — 独立于 Agent/SubAgent 的可复用组件。
///
/// 线程安全：不安全。同一实例不应并发调用。
class ToolCallLoop {
public:
    /// @param client   LLM 客户端（外部管理生命周期）
    /// @param registry 工具注册中心（外部管理生命周期）
    ToolCallLoop(llm::ILLMClient& client, ToolRegistry& registry);

    /// 执行 tool call 循环。
    ///
    /// @param conversation  对话历史（会被修改：追加 assistant + tool 消息）
    /// @param tools         可用工具定义列表
    /// @param system_prompt 系统提示词
    /// @param callbacks     流式回调（首轮使用，后续轮次为非流式）
    /// @param config        循环配置
    /// @return              最终结果（含响应、超时状态、错误信息）
    ToolCallLoopResult run(
        llm::Conversation& conversation,
        const std::vector<llm::ToolDefinition>& tools,
        const std::string& system_prompt,
        llm::StreamCallbacks callbacks,
        const ToolCallLoopConfig& config = {});

    /// 获取底层 LLM 客户端（供调试）。
    llm::ILLMClient& client() { return client_; }

    /// 获取工具注册中心（供调试）。
    ToolRegistry& registry() { return registry_; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
};

} // namespace agent
