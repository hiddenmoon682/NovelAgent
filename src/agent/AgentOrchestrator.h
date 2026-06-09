#pragma once

#include "agent/SubAgent.h"
#include "llm/ILLMClient.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace agent {

class ToolRegistry;
class TemplateManager;

/// 子任务数据模型。
struct SubTask {
    std::string id;
    std::string description;
    std::string system_prompt;
    std::vector<std::string> allowed_tools;
    std::string result;       // 执行结果文本
    std::string status;       // pending | running | completed | failed | timed_out
    std::string error;
};

/// 多 Agent 并行编排器。
///
/// 接收复杂任务 → 拆分为子任务 → 并行执行 → 汇总结果。
/// 支持模板驱动的子 Agent 配置和运行时 /parallel 开关。
class AgentOrchestrator {
public:
    /// @param client      LLM 客户端（主 Agent 和子 Agent 共享）
    /// @param registry    工具注册中心
    /// @param mainPrompt  主 Agent 的 system prompt（用于任务分解和结果汇总）
    AgentOrchestrator(llm::ILLMClient& client, ToolRegistry& registry,
                      std::string mainPrompt = "");

    /// 处理用户消息：判断是否需要并行，是则拆解执行。
    std::string processMessage(const std::string& input);

    // ── 配置 ──

    void setParallelEnabled(bool on) { parallel_enabled_ = on; }
    bool isParallelEnabled() const { return parallel_enabled_; }
    void setMaxParallel(int n) { max_parallel_ = n; }
    int maxParallel() const { return max_parallel_; }

    /// 设置模板管理器（可选，用于模板感知的任务分解）。
    void setTemplateManager(TemplateManager* tm) { template_mgr_ = tm; }

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    std::string main_prompt_;
    bool parallel_enabled_ = true;
    int max_parallel_ = 4;
    TemplateManager* template_mgr_ = nullptr;

    /// 让 LLM 判断是否需要并行 + 拆解子任务
    std::vector<SubTask> decompose(const std::string& input);

    /// 并行执行所有子任务（std::async）
    void executeParallel(std::vector<SubTask>& tasks);

    /// 让 LLM 汇总子任务结果
    std::string synthesize(const std::vector<SubTask>& tasks);
};

} // namespace agent
