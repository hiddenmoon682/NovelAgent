#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/TokenCounter.h"
#include "project/Models.h"
#include "prompt/PromptContextBuilder.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

// ===========================================================================
// assemble — 一站式上下文组装
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int context_window,
    const Project* project,
    const std::string& chapter_id)
{
    ContextAssembly result;
    result.budget = calculateBudget(context_window);

    // 1. 构建系统提示词
    if (project) {
        result.system_prompt = buildSystemPrompt(*project, chapter_id);
        if (result.system_prompt.empty()) {
            spdlog::warn("[ContextManager] 系统提示词为空 — 项目或章节信息不足");
        }
    }

    // 2. 计算系统提示词占用的 token
    int sys_tokens = result.system_prompt.empty()
        ? 0
        : llm::TokenCounter::countTokens(result.system_prompt);

    // 3. 消息预算 = 总预算 - 系统提示词
    int msg_budget = std::max(0, result.budget - sys_tokens);

    // 4. 截断消息
    result.messages = truncateMessages(
        conversation.messages(), msg_budget, result.truncated_count);
    result.truncated = (result.truncated_count > 0);

    if (result.truncated) {
        spdlog::info("[ContextManager] 截断 {} 条消息 (预算={}, 系统={}, 消息预算={})",
                     result.truncated_count, result.budget, sys_tokens, msg_budget);
    }

    // 5. 统计实际 token
    int msg_tokens = llm::TokenCounter::countMessages(result.messages);
    result.total_tokens = sys_tokens + msg_tokens;

    return result;
}

// ===========================================================================
// buildSystemPrompt — 委托 PromptContextBuilder
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project,
    const std::string& chapter_id)
{
    // 构造 PromptContextOptions
    prompt::PromptContextOptions options;
    options.task = "write_chapter";

    if (!chapter_id.empty()) {
        options.chapter_id = chapter_id;
    } else if (!project.outline.chapters.empty()) {
        // 未指定章节时，默认使用大纲中的第一章（如果有）
        // 但此时可能不需要章节上下文——返回基础项目信息
        spdlog::debug("[ContextManager] 未指定 chapter_id，跳过章节上下文构建");
    }

    if (chapter_id.empty()) {
        // 无指定章节时，构造最小化的系统提示词（仅项目概述）
        std::string prompt;
        prompt += "# 项目: " + project.title + "\n";
        if (!project.logline.empty()) {
            prompt += "Logline: " + project.logline + "\n";
        }
        if (!project.theme.empty()) {
            prompt += "主题: " + project.theme + "\n";
        }
        return prompt;
    }

    auto ctx = prompt::PromptContextBuilder::buildForChapter(project, options);
    if (!ctx) {
        spdlog::warn("[ContextManager] 无法为章节 '{}' 构建上下文，回退到项目概述", chapter_id);
        return buildSystemPrompt(project); // fallback 到无章节版本
    }

    spdlog::debug("[ContextManager] 系统提示词构建完成 — 章节={}, 长度={}",
                  chapter_id, ctx->rendered_prompt.size());
    return ctx->rendered_prompt;
}

// ===========================================================================
// calculateBudget — 80/20 规则
// ===========================================================================

int ContextManager::calculateBudget(int context_window)
{
    // 80% 用于输入，20% 留给模型输出
    // Phase 4 将引入更细粒度的分配：
    //   50% 当前章节 + 大纲角色信息
    //   30% 最近对话
    //   20% 历史摘要
    return static_cast<int>(context_window * 0.8);
}

// ===========================================================================
// truncateMessages — 按预算从旧到新截断
// ===========================================================================

std::vector<llm::Message> ContextManager::truncateMessages(
    const std::vector<llm::Message>& messages,
    int budget,
    int& truncated_count)
{
    truncated_count = 0;

    if (messages.empty()) {
        return messages;
    }

    // 预算非正数时，无法容纳任何消息
    if (budget <= 0) {
        truncated_count = static_cast<int>(messages.size());
        return {};
    }

    // 无需截断
    if (llm::TokenCounter::countMessages(messages) <= budget) {
        return messages;
    }

    // 从尾部（最新消息）向前构建结果，O(n) 单次遍历。
    // 每轮 LLM 调用最需要的是最近的消息（当前对话上下文），
    // 旧消息最先被丢弃。
    std::vector<llm::Message> result;
    int used = 0;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        int msg_cost = llm::TokenCounter::countMessages({*it});
        if (used + msg_cost > budget) break;
        used += msg_cost;
        result.push_back(*it);
    }
    // 反转为原始顺序（旧→新）
    std::reverse(result.begin(), result.end());
    truncated_count = static_cast<int>(messages.size()) - static_cast<int>(result.size());

    // 确保至少保留最后一条消息（通常是用户的最后输入）
    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }

    return result;
}

} // namespace agent
