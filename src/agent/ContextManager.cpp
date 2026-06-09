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
        spdlog::error("[ContextManager] 无法为章节 '{}' 构建上下文", chapter_id);
        return {};
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

    // 空列表或预算充足 → 直接返回
    if (messages.empty() || budget <= 0) {
        return messages;
    }

    int total = llm::TokenCounter::countMessages(messages);
    if (total <= budget) {
        return messages; // 无需截断
    }

    // 从头部（最旧）开始移除，直到剩余 token ≤ budget
    // 跳过 system 角色（但 messages 中通常不含 system——已经在 system_prompt 中）
    std::vector<llm::Message> result = messages;
    while (!result.empty() && total > budget) {
        total -= llm::TokenCounter::countTokens(result.front().content);
        // 加上结构开销的估算（角色标记 + 可能的 tool_calls）
        total -= 4; // 大约每个消息的结构开销
        result.erase(result.begin());
        ++truncated_count;
    }

    // 确保至少保留最后一条消息（通常是用户的最后输入）
    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }

    return result;
}

} // namespace agent
