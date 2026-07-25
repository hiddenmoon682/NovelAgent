// ============================================================
// ContextBudgetEvaluator.cpp — Token 预算评估器实现
//
// 功能：从 Memory 读取对话历史消息，统计 token 数，
//       并根据 TokenBudget 评估当前上下文用量状态
//       （正常/警告/自动压缩/致命错误）。
// ============================================================

#include "agent/context/ContextBudgetEvaluator.h"

#include "agent/context/IMemory.h"
#include "llm/TokenCounter.h"

#include <spdlog/spdlog.h>

namespace agent {

// ------------------------------------------------------------------
// evaluate — 上下文预算评估主流程
//
// 步骤：
//   1. 统计 Memory 中消息列表的 token 数；
//   2. 若 budget 设置了 model_limit，根据总用量评估上下文状态。
// ------------------------------------------------------------------
BudgetEvaluationResult ContextBudgetEvaluator::evaluate(
    const llm::IMemory& memory,
    const TokenBudget& budget,
    const std::string& system_prompt,
    const std::string& model_name,
    const llm::TokenCounter* calibrator) const
{
    BudgetEvaluationResult result;

    // 统计对话消息的 token 数（支持校准）
    result.message_tokens = llm::TokenCounter::countMessagesCalibrated(
        memory.messages(), model_name, calibrator);

    // system prompt 也会发送给 API，需要计入总用量
    int system_tokens = llm::TokenCounter::countTokens(system_prompt);
    result.total_tokens = result.message_tokens + system_tokens;

    // 仅在设置了 model_limit 时评估上下文用量
    if (budget.model_limit > 0) {
        // 根据总用量判定状态（Normal / Warning / Critical / AutoCompact / Error）
        result.status = budget.evaluate(result.total_tokens);
        int pct = budget.usagePercent(result.total_tokens);

        switch (result.status) {
        case ContextStatus::Error:
            // 致命超限：应阻止后续 LLM 调用，提示用户手动 compact 或 clear
            result.fatal = true;
            result.warnings.push_back(
                "致命错误：上下文用量已超过模型上限（"
                + std::to_string(pct) + "%，"
                + std::to_string(result.total_tokens) + " / "
                + std::to_string(budget.model_limit)
                + " tokens）。请使用 /compact 压缩对话历史，或 /clear 清空对话后重试。");
            break;
        case ContextStatus::AutoCompact:
            result.warnings.push_back(
                "上下文用量已达 " + std::to_string(pct) +
                "%，建议执行 /compact 压缩对话历史。");
            break;
        case ContextStatus::Critical:
            result.warnings.push_back(
                "上下文用量已达 " + std::to_string(pct) +
                "%，接近模型上限。建议使用 /compact 压缩对话历史。");
            break;
        case ContextStatus::Warning:
            result.warnings.push_back(
                "上下文用量 " + std::to_string(pct) +
                "%，可考虑 /compact 释放空间。");
            break;
        default:
            break;
        }
    }

    return result;
}

} // namespace agent
