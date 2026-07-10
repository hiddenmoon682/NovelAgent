#pragma once

// 子 Agent — 独立对话上下文 + 受限工具集 + 超时保护 + 线程隔离。
//
// P0 架构改进：
// - 通过 IToolProvider& 而非 ToolRegistry& 访问工具
// - RestrictedToolProvider 在类型系统层面保证安全约束
// - 使用 ToolCallLoop 复用 tool call 循环引擎
//
// Phase 4 线程安全：
// - 每个 SubAgent 通过 LLMClientFactory 创建独立的 LLMClient 实例
// - 并行 SubAgent 之间不共享 HTTP 连接状态

#include "agent/ExecutionTracer.h"
#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace llm {
class LLMClientFactory;
} // namespace llm

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
    bool cancelled = false;        //< Issue 26: 外部取消信号触发
    std::string error;
    int input_tokens = 0;          //< Issue 28: 子任务 LLM 调用的输入 token 数
    int output_tokens = 0;         //< Issue 28: 子任务 LLM 调用的输出 token 数
    std::string trace_summary;     //< A3: 子任务执行轨迹摘要（JSON 字符串），供父 Agent 日志/调试使用
};

// 子 Agent — 拥有独立的 LLMClient 实例，实现线程隔离。
//
// 每个 SubAgent 通过 LLMClientFactory 创建自己的 LLMClient，
// 确保并行子任务之间不会共享 HTTP 连接状态。
class SubAgent {
public:
    // @param factory   LLM 客户端工厂（用于创建独立 LLMClient 实例）
    // @param tools     工具提供者（受限视图，只能调用白名单工具）
    SubAgent(llm::LLMClientFactory& factory, IToolProvider& tools);

    // 测试用构造函数：直接注入已创建的 ILLMClient 实例。
    // @param client    已创建的 LLMClient（所有权转移）
    // @param tools     工具提供者
    SubAgent(std::unique_ptr<llm::ILLMClient> client, IToolProvider& tools);

    // 执行子任务，阻塞直到完成或超时。
    SubAgentResult execute(const SubAgentConfig& config);

    // A3: 设置外部 tracer（可选），其摘要将随结果一并返回。
    void setTracer(ExecutionTracer* tracer) { external_tracer_ = tracer; }

    const llm::Conversation& conversation() const { return conversation_; }
    const ExecutionTracer& tracer() const { return tracer_; }

private:
    std::unique_ptr<llm::ILLMClient> client_;  // 独立 LLMClient 实例
    IToolProvider& tools_;
    llm::Conversation conversation_;
    std::mutex conv_mutex_;               // 保护 conversation_ 并发访问
    std::atomic<bool> cancelled_{false};  // 超时时通知异步任务停止
    ExecutionTracer tracer_;              // A3: 子任务执行轨迹记录器，由 setTracer 或 execute 内部使用
    ExecutionTracer* external_tracer_ = nullptr;  // A3: 外部注入的 tracer（可选），非拥有
};

} // namespace agent
