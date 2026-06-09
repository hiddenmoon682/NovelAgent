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

// ============================================================================
// 子任务数据模型
// ============================================================================

struct SubTask {
    std::string id, description, system_prompt;
    std::vector<std::string> allowed_tools;
    std::string result, status, error;  // status: pending|running|completed|failed|timed_out
};

// ============================================================================
// 策略接口
// ============================================================================

/// 并行检测策略 — 判断用户输入是否应该触发并行编排。
class IParallelDetector {
public:
    virtual ~IParallelDetector() = default;
    virtual bool shouldParallelize(const std::string& input) const = 0;
};

/// 关键词启发式检测器（默认）。
class KeywordParallelDetector : public IParallelDetector {
public:
    bool shouldParallelize(const std::string& input) const override {
        return input.find("所有") != std::string::npos ||
               input.find("检查") != std::string::npos ||
               input.find("分析") != std::string::npos;
    }
};

/// 任务分解策略 — 将用户输入拆分为子任务列表。
class IDecompositionStrategy {
public:
    virtual ~IDecompositionStrategy() = default;
    virtual std::vector<SubTask> decompose(const std::string& input,
                                            const std::string& mainPrompt) = 0;
};

/// 模板驱动分解（当前默认）。
class TemplateDecomposition : public IDecompositionStrategy {
    TemplateManager* template_mgr_;
public:
    explicit TemplateDecomposition(TemplateManager* tm) : template_mgr_(tm) {}
    std::vector<SubTask> decompose(const std::string& input,
                                    const std::string& mainPrompt) override;
};

/// SubAgent 工厂 — 创建子 Agent 实例（可注入 Mock 用于测试）。
using SubAgentFactory = std::function<std::unique_ptr<SubAgent>(
    llm::ILLMClient&, ToolRegistry&)>;

inline auto defaultSubAgentFactory() {
    return [](llm::ILLMClient& c, ToolRegistry& r) {
        return std::make_unique<SubAgent>(c, r);
    };
}

// ============================================================================
// AgentOrchestrator
// ============================================================================

class AgentOrchestrator {
public:
    AgentOrchestrator(llm::ILLMClient& client, ToolRegistry& registry,
                      std::string mainPrompt = "");

    std::string processMessage(const std::string& input);

    // ── 配置 ──
    void setParallelEnabled(bool on) { parallel_enabled_ = on; }
    bool isParallelEnabled() const { return parallel_enabled_; }
    void setMaxParallel(int n) { max_parallel_ = n; }
    int maxParallel() const { return max_parallel_; }

    void setTemplateManager(TemplateManager* tm);

    /// 注入自定义策略（默认使用 KeywordParallelDetector + TemplateDecomposition）
    void setParallelDetector(std::unique_ptr<IParallelDetector> detector);
    void setDecompositionStrategy(std::unique_ptr<IDecompositionStrategy> strategy);
    void setSubAgentFactory(SubAgentFactory factory);

private:
    llm::ILLMClient& client_;
    ToolRegistry& registry_;
    std::string main_prompt_;
    bool parallel_enabled_ = true;
    int max_parallel_ = 4;

    TemplateManager* template_mgr_ = nullptr;
    std::unique_ptr<IParallelDetector> detector_;
    std::unique_ptr<IDecompositionStrategy> decomposition_;
    SubAgentFactory agent_factory_ = defaultSubAgentFactory();

    IParallelDetector& getDetector();
    IDecompositionStrategy& getDecomposition();

    std::vector<SubTask> decompose(const std::string& input);
    void executeParallel(std::vector<SubTask>& tasks);
    std::string synthesize(const std::vector<SubTask>& tasks);
};

} // namespace agent
