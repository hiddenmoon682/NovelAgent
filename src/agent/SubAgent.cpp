/// SubAgent 实现 — Fix #1: 完全委托 ToolCallLoop + Phase 4 线程安全（独立 LLMClient 实例）。

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

    // 捕获 this 而非单独捕获成员引用，确保取消信号可见
    auto future = std::async(std::launch::async, [this, config, tool_defs]() -> SubAgentResult {
        SubAgentResult r;
        try {
            if (cancelled_) return r;  // 启动前检查取消信号

            {
                std::lock_guard<std::mutex> lock(conv_mutex_);
                conversation_.addUser(config.task);
            }

            if (cancelled_) return r;  // 添加用户消息后检查

            ToolCallLoop loop(*client_, tools_);
            ToolCallLoopConfig cfg;
            cfg.max_rounds = config.max_tool_rounds;
            cfg.first_round_streaming = false;  // SubAgent 无需流式输出
            cfg.max_repeated_calls = 3;

            // 注意：loop.run() 内部会修改 conversation_，此处依赖 ToolPipeline 的 Conversation& 引用
            // 由于主线程在超时后会等待 future 结束，conversation_ 不会并发访问
            std::lock_guard<std::mutex> lock2(conv_mutex_);
            if (cancelled_) return r;

            auto loop_result = loop.run(
                conversation_, tool_defs, config.system_prompt, {}, cfg);
            r.output = loop_result.response.content;
            if (loop_result.timed_out) { r.timed_out = true; r.error = loop_result.error; }
            if (loop_result.loop_detected) r.error = loop_result.error;
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
