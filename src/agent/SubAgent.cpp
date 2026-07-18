// SubAgent 实现 — Fix #1: 完全委托 ToolCallLoop + Phase 4 线程安全（独立 LLMClient 实例）。

#include "agent/SubAgent.h"
#include "agent/ToolCallLoop.h"
#include "llm/LLMClientFactory.h"

#include <spdlog/spdlog.h>
#include <future>

namespace agent {

SubAgent::SubAgent(llm::LLMClientFactory& factory, IToolProvider& tools)
    : client_(factory.create()), tools_(tools)
{}

SubAgent::SubAgent(std::unique_ptr<llm::ILLMClient> client, IToolProvider& tools)
    : client_(std::move(client)), tools_(tools)
{}

SubAgentResult SubAgent::execute(const SubAgentConfig& config)
{
    SubAgentResult result;
    auto tool_defs = tools_.getDefinitions();

    {
        std::lock_guard<std::mutex> lock(conv_mutex_);
        conversation_.clear();
    }

    spdlog::info("[SubAgent] 开始: {} (工具数={})",
                 config.task.substr(0, 60), tool_defs.size());

    cancelled_ = false;

    // Issue 13 修复：使用栈上临时 Conversation 执行 tool_call 循环，
    // 仅在最终合并结果时持锁。避免 lock_guard 跨越 HTTP 调用（数分钟）。
    auto future = std::async(std::launch::async, [this, config, tool_defs]() -> SubAgentResult {
        SubAgentResult r;
        try {
            if (cancelled_) return r;

            // 步骤 1: 在本地 Conversation 上添加用户消息（无锁）
            llm::Conversation localConv;
            localConv.addUser(config.task);

            if (cancelled_) return r;

            // 步骤 2: 在本地 Conversation 上执行 tool_call 循环（无锁）
            tracer_.clear();
            ToolCallLoop loop(*client_, tools_);
            loop.setCancelled(&cancelled_);
            ToolCallLoopConfig cfg;
            cfg.setMaxRounds(config.max_tool_rounds)
               .setMaxRepeatedCalls(3);
            cfg.hooks.on_round_complete = [&r](int input, int output, int) {
                r.input_tokens += input;
                r.output_tokens += output;
            };

            auto loop_result = loop.run(
                localConv, tool_defs, config.system_prompt, {}, cfg);

            r.output = loop_result.response.content;
            if (loop_result.loop_detected) r.error = loop_result.error;
            if (loop_result.cancelled) { r.cancelled = true; r.error = loop_result.error; }
            // A3: 捕获轨迹摘要 — json summary() 转 string，供父 Agent 日志/调试
            r.trace_summary = tracer_.summary().dump();

            // 步骤 3: 短暂持锁，批量合并 localConv → conversation_
            {
                std::lock_guard<std::mutex> lock(conv_mutex_);
                for (const auto& msg : localConv.all()) {
                    conversation_.add(msg);
                }
            }
        } catch (const std::exception& e) {
            r.error = e.what();
            spdlog::error("[SubAgent] 异常: {}", e.what());
        }
        return r;
    });

    auto status = future.wait_for(config.timeout);
    if (status == std::future_status::timeout) {
        cancelled_ = true;
        spdlog::warn("[SubAgent] 超时（{}s），通知异步任务取消并等待其退出…",
                     config.timeout.count());

        // B3 修复：无条件等待异步任务完全退出（不放弃），避免 this 悬空。
        // cancelled_=true 已通知任务尽快结束（在每次 HTTP 调用返回后检查）；
        // HTTP 客户端自身有 read_timeout（180s），不会真正无限挂起。
        // 此处阻塞时间 = 剩余 HTTP 调用时长（≤ read_timeout），远好于 use-after-free。
        // SubAgent 调用方（AgentOrchestrator）本身在独立线程中运行，Blocking 不影响主 Agent。
        future.wait();
        result = future.get();
        spdlog::info("[SubAgent] 超时后异步任务已完成");

        result.timed_out = true;
        if (result.error.empty())
            result.error = "子任务超时 (" + std::to_string(config.timeout.count()) + "s)";
        return result;
    }
    return future.get();
}

} // namespace agent
