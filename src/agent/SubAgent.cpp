/// SubAgent 实现 — Fix #1: 完全委托 ToolCallLoop。

#include "agent/SubAgent.h"
#include "agent/ToolCallLoop.h"

#include <spdlog/spdlog.h>
#include <future>

namespace agent {

SubAgent::SubAgent(llm::ILLMClient& client, IToolProvider& tools)
    : client_(client), tools_(tools)
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

            ToolCallLoop loop(client_, tools_);
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
        spdlog::warn("[SubAgent] 超时，等待异步任务结束…");

        // 等待异步任务注意到取消信号后结束（最多再等 10 秒）
        auto cleanup_status = future.wait_for(std::chrono::seconds(10));
        if (cleanup_status == std::future_status::timeout) {
            spdlog::error("[SubAgent] 异步任务未能及时取消，强制等待");
            future.wait();  // 最终必须等待，避免引用悬空
        } else {
            // 任务正常结束，获取结果
            result = future.get();
        }

        result.timed_out = true;
        if (result.error.empty())
            result.error = "子任务超时 (" + std::to_string(config.timeout.count()) + "s)";
        return result;
    }
    return future.get();
}

} // namespace agent
