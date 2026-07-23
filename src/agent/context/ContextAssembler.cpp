#include "agent/context/ContextAssembler.h"

#include "agent/context/IMemory.h"
#include "agent/prompt/PromptContextBuilder.h"
#include "llm/TokenCounter.h"
#include "project/Models.h"

#include <spdlog/spdlog.h>

namespace agent {

std::string ContextAssembler::buildSystemPrompt(const Project& project) {
    std::string prompt;
    prompt += "# 项目: " + project.title + "\n";
    if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
    if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";

    prompt += "\n" + prompt::PromptContextBuilder::renderToolUseInstructions();

    return prompt;
}

AssemblyResult ContextAssembler::assemble(
    const Project* project,
    const llm::IMemory& memory,
    const TokenBudget& budget,
    const std::string& model_name,
    const llm::TokenCounter* calibrator) const
{
    AssemblyResult result;

    if (project) {
        result.system_prompt = buildSystemPrompt(*project);
    }

    result.system_tokens = llm::TokenCounter::countTokensCalibrated(
        result.system_prompt, model_name, calibrator);
    result.message_tokens = llm::TokenCounter::countMessagesCalibrated(
        memory.messages(), model_name, calibrator);
    result.total_tokens = result.system_tokens + result.message_tokens;

    if (budget.model_limit > 0) {
        int msg_budget = budget.model_limit - result.system_tokens;
        if (msg_budget <= 0) {
            result.warnings.push_back(
                "System prompt 已占用全部预算（" + std::to_string(result.system_tokens) +
                " tokens），无法容纳对话历史。建议精简项目上下文或增加 max_context_tokens。");
            spdlog::error("[ContextAssembler] msg_budget <= 0 (sys={}, max={})",
                          result.system_tokens, budget.model_limit);
        }

        result.status = budget.evaluate(result.total_tokens);
        int pct = budget.usagePercent(result.total_tokens);

        switch (result.status) {
        case ContextStatus::Error:
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
