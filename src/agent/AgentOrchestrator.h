#pragma once

// 多 Agent 并行编排器。
//
// P1 架构改进：
// - ISynthesisStrategy 接口解耦汇总逻辑（LLM/拼接/模板可替换）
// - SubTask 从 AgentOrchestratorTypes.h 导入（共享类型）
// - SubAgent 通过 IToolProvider 受限视图访问工具
//
// Phase 4 线程安全改进：
// - AgentOrchestrator 持有独立的 LLMClient（通过工厂创建），与主 Agent 隔离
// - 每个并行 SubAgent 通过工厂获得独立的 LLMClient 实例

#include "agent/AgentOrchestratorTypes.h"
#include "agent/ISynthesisStrategy.h"
#include "agent/SubAgent.h"
#include "agent/ThreadPool.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llm {
class ILLMClient;
class LLMClientFactory;
} // namespace llm

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
        // A18 修复：负向规则 — 明确的写作/修订意图不触发并行编排
        if (input.find("语气") != std::string::npos ||
            input.find("错别字") != std::string::npos ||
            input.find("改") != std::string::npos ||
            input.find("写") != std::string::npos) return false;
        // 正向规则：双关键词联合命中才触发并行
        int hits = 0;
        if (input.find("所有") != std::string::npos) ++hits;
        if (input.find("检查") != std::string::npos) ++hits;
        if (input.find("分析") != std::string::npos) ++hits;
        return hits >= 2;
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
    llm::LLMClientFactory&, IToolProvider&)>;

inline auto defaultSubAgentFactory() {
    return [](llm::LLMClientFactory& f, IToolProvider& t) {
        return std::make_unique<SubAgent>(f, t);
    };
}

// ============================================================================
// AgentOrchestrator
// ============================================================================

class AgentOrchestrator {
public:
    // @param factory  LLM 客户端工厂（用于创建编排器自身及子 Agent 的独立客户端）
    AgentOrchestrator(llm::LLMClientFactory& factory, ToolRegistry& registry,
                      std::string mainPrompt = "");
    ~AgentOrchestrator();

    std::string processMessage(const std::string& input);

    // ── 配置 ──
    void setParallelEnabled(bool on) { parallel_enabled_ = on; }
    bool isParallelEnabled() const { return parallel_enabled_; }
    void setMaxParallel(int n) { max_parallel_ = n; }
    int maxParallel() const { return max_parallel_; }

    // D6: 最近一次 processMessage 调用累计的 token 用量
    // （含串行回退/汇总 LLM 调用；子任务 token 单独统计在 sub_* 字段中）。
    // 供 ParallelProcessor 调 recordUsage 恢复并行模式的上下文预算管理。
    int lastInputTokens() const { return last_input_tokens_; }
    int lastOutputTokens() const { return last_output_tokens_; }

    // Issue 28: 最近一次 processMessage 中子任务累计的 token 用量。
    // 供 ParallelProcessor 汇总完整的上下文预算。
    int lastSubInputTokens() const { return last_sub_input_tokens_; }
    int lastSubOutputTokens() const { return last_sub_output_tokens_; }

    // CRIT-1: 最近一次 processMessage 中所有子任务的状态/结果详情。
    // 供 ParallelProcessor 注入对话历史，使后续 LLM 轮次能看到子任务工具调用链。
    const std::vector<SubTask>& lastSubTasks() const { return last_sub_tasks_; }

    void setTemplateManager(TemplateManager* tm);

    // A18.3: 运行时更新主提示词（供 ParallelProcessor 注入动态上下文）。
    void setMainPrompt(const std::string& p) { main_prompt_ = p; }

    // 注入自定义策略
    void setParallelDetector(std::unique_ptr<IParallelDetector> detector);
    void setDecompositionStrategy(std::unique_ptr<IDecompositionStrategy> strategy);
    void setSynthesisStrategy(std::unique_ptr<ISynthesisStrategy> strategy);
    void setSubAgentFactory(SubAgentFactory factory);

    // Issue 4: 注入外部线程池（nullptr 时使用内置默认池）。
    void setThreadPool(ThreadPool* pool) { thread_pool_ = pool ? pool : &default_pool_; }

private:
    llm::LLMClientFactory& factory_;                      // 供 SubAgent 创建独立客户端
    std::unique_ptr<llm::ILLMClient> client_;             // 编排器自身使用的 LLMClient
    ToolRegistry& registry_;
    std::string main_prompt_;
    bool parallel_enabled_ = true;
    int max_parallel_ = 4;

    // D6: 最近一次 processMessage 的 token 累计（供 ParallelProcessor 恢复预算管理）
    int last_input_tokens_ = 0;
    int last_output_tokens_ = 0;
    int last_sub_input_tokens_ = 0;   // Issue 28: 子任务累计输入 token
    int last_sub_output_tokens_ = 0;  // Issue 28: 子任务累计输出 token
    std::vector<SubTask> last_sub_tasks_;  // CRIT-1: 最近一次子任务详情

    TemplateManager* template_mgr_ = nullptr;
    std::unique_ptr<IParallelDetector> detector_;
    std::unique_ptr<IDecompositionStrategy> decomposition_;
    std::unique_ptr<ISynthesisStrategy> synthesis_;
    SubAgentFactory agent_factory_ = defaultSubAgentFactory();
    ThreadPool default_pool_;          // Issue 4: 内置默认线程池（12 线程）
    ThreadPool* thread_pool_ = &default_pool_;  // 当前使用的池（可外部注入覆盖）

    IParallelDetector& getDetector();
    IDecompositionStrategy& getDecomposition();
    ISynthesisStrategy& getSynthesis();

    std::vector<SubTask> decompose(const std::string& input);
    void executeParallel(std::vector<SubTask>& tasks);
    std::string synthesize(const std::vector<SubTask>& tasks);
};

} // namespace agent
