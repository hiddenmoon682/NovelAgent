#pragma once

/// 多 Agent 并行编排器。
///
/// P1 架构改进：
/// - ISynthesisStrategy 接口解耦汇总逻辑（LLM/拼接/模板可替换）
/// - SubTask 从 AgentOrchestratorTypes.h 导入（共享类型）
/// - SubAgent 通过 IToolProvider 受限视图访问工具

#include "agent/AgentOrchestratorTypes.h"
#include "agent/ISynthesisStrategy.h"
#include "agent/SubAgent.h"
#include "llm/ILLMClient.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace agent {

class IToolProvider;
class ToolRegistry;
class TemplateManager;

// ============================================================================
// 策略接口
// ============================================================================

class IParallelDetector {
public:
    virtual ~IParallelDetector() = default;
    virtual bool shouldParallelize(const std::string& input) const = 0;
};

class KeywordParallelDetector : public IParallelDetector {
public:
    bool shouldParallelize(const std::string& input) const override {
        return input.find("所有") != std::string::npos ||
               input.find("检查") != std::string::npos ||
               input.find("分析") != std::string::npos;
    }
};

class IDecompositionStrategy {
public:
    virtual ~IDecompositionStrategy() = default;
    virtual std::vector<SubTask> decompose(const std::string& input,
                                            const std::string& mainPrompt) = 0;
};

class TemplateDecomposition : public IDecompositionStrategy {
    TemplateManager* template_mgr_;
public:
    explicit TemplateDecomposition(TemplateManager* tm) : template_mgr_(tm) {}
    std::vector<SubTask> decompose(const std::string& input,
                                    const std::string& mainPrompt) override;
};

using SubAgentFactory = std::function<std::unique_ptr<SubAgent>(
    llm::ILLMClient&, IToolProvider&)>;

inline auto defaultSubAgentFactory() {
    return [](llm::ILLMClient& c, IToolProvider& t) {
        return std::make_unique<SubAgent>(c, t);
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

    /// 注入自定义策略
    void setParallelDetector(std::unique_ptr<IParallelDetector> detector);
    void setDecompositionStrategy(std::unique_ptr<IDecompositionStrategy> strategy);
    void setSynthesisStrategy(std::unique_ptr<ISynthesisStrategy> strategy);
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
    std::unique_ptr<ISynthesisStrategy> synthesis_;
    SubAgentFactory agent_factory_ = defaultSubAgentFactory();

    IParallelDetector& getDetector();
    IDecompositionStrategy& getDecomposition();
    ISynthesisStrategy& getSynthesis();

    std::vector<SubTask> decompose(const std::string& input);
    void executeParallel(std::vector<SubTask>& tasks);
    std::string synthesize(const std::vector<SubTask>& tasks);
};

} // namespace agent
