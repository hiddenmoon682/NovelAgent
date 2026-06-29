#pragma once

/// 子任务汇总策略抽象接口 — 解耦汇总逻辑与 AgentOrchestrator。
///
/// 架构改进（P1）：AgentOrchestrator::synthesize() 硬编码 LLM 调用，
/// 改为通过策略接口注入，支持 LLM汇总/简单拼接/模板渲染 等多种方式。

#include "agent/AgentOrchestratorTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llm {
class ILLMClient;
} // namespace llm

namespace agent {

/// 子任务汇总策略抽象接口。
class ISynthesisStrategy {
public:
    virtual ~ISynthesisStrategy() = default;

    /// 汇总子任务执行结果。
    /// @param tasks          已完成的子任务列表
    /// @param original_query 用户的原始查询
    /// @return               汇总后的文本
    virtual std::string synthesize(
        const std::vector<SubTask>& tasks,
        const std::string& original_query) = 0;
};

/// LLM 驱动汇总 — 调用 LLM 总结子任务结果（当前默认行为）。
/// 使用 AgentOrchestrator 拥有的独立 LLMClient 引用，不与其他组件共享。
class LlmSynthesis : public ISynthesisStrategy {
public:
    /// @param client       LLM 客户端（来自 AgentOrchestrator 的独立实例，LlmSynthesis 不持有所有权）
    /// @param main_prompt  主 Agent 的 system prompt
    /// @param max_result_chars 每个子任务结果的最大展示字符数（默认 3000）
    LlmSynthesis(llm::ILLMClient& client, std::string main_prompt,
                 int max_result_chars = 3000);

    std::string synthesize(
        const std::vector<SubTask>& tasks,
        const std::string& original_query) override;

private:
    llm::ILLMClient& client_;
    std::string main_prompt_;
    int max_result_chars_;
};

/// 简单拼接汇总 — 不调用 LLM，直接将子任务结果拼接返回。
class ConcatSynthesis : public ISynthesisStrategy {
public:
    std::string synthesize(
        const std::vector<SubTask>& tasks,
        const std::string& original_query) override;
};

/// 自定义函数汇总 — 通过 std::function 注入任意汇总逻辑。
class CustomSynthesis : public ISynthesisStrategy {
public:
    using Fn = std::function<std::string(
        const std::vector<SubTask>&, const std::string&)>;
    explicit CustomSynthesis(Fn fn) : fn_(std::move(fn)) {}

    std::string synthesize(
        const std::vector<SubTask>& tasks,
        const std::string& original_query) override {
        return fn_(tasks, original_query);
    }

private:
    Fn fn_;
};

} // namespace agent
