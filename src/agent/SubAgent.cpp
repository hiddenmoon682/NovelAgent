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
        spdlog::warn("[SubAgent] 超时（{}s），通知异步任务取消…",
                     config.timeout.count());

        // 等待异步任务注意到 cancelled_ 后自行退出。
        // 异步任务可能正阻塞在 HTTP 调用中，最坏需等待 HTTP read_timeout（180s）
        // 才能返回并检查 cancelled_。使用两倍 timeout 作为清理宽限期，
        // 确保覆盖 HTTP 超时窗口，同时避免原先 future.wait() 的无限阻塞。
        auto cleanup_status = future.wait_for(config.timeout * 2);
        if (cleanup_status == std::future_status::timeout) {
            spdlog::error("[SubAgent] 异步任务在取消后 {}s 仍未完成，"
                          "HTTP 调用疑似挂起。为避免调用方永久阻塞，放弃等待。",
                          config.timeout.count() * 2);
            // 极端情况：任务中的 HTTP 调用超过了 read_timeout 仍无响应。
            // lambda 捕获了 [this]，此 SubAgent 返回后将被销毁，
            // 但 cancelled_ 已设置，任务线程将在当前 HTTP 调用结束后尽快退出。
        } else {
            result = future.get();
            spdlog::info("[SubAgent] 取消后任务在 {}s 内完成", config.timeout.count());
        }

        result.timed_out = true;
        if (result.error.empty())
            result.error = "子任务超时 (" + std::to_string(config.timeout.count()) + "s)";
        return result;
    }
    return future.get();
}

} // namespace agent
