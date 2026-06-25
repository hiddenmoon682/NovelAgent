/// ContextManager 实现 — 精简版。
///
/// 职责：
///   1. 构建动态 system prompt（项目/章节上下文）
///   2. 按 token 预算截断对话历史
///   3. 会话持久化

#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "project/IStorageBackend.h"
#include "project/Models.h"
#include "agent/PromptContextBuilder.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

// ===========================================================================
// 构造
// ===========================================================================

namespace {
/// 默认存储占位符（不使用持久化功能时的回退）。
IStorageBackend& defaultStorage() {
    static FileStorageBackend s("");
    return s;
}
} // namespace

ContextManager::ContextManager()
    : storage_(defaultStorage())
    , persistence_(storage_)
{}

ContextManager::ContextManager(IStorageBackend& storage)
    : storage_(storage)
    , persistence_(storage)
{}

// ===========================================================================
// assemble — 一站式入口（精简版）
//
// 流程：
//   1. 构建 system prompt（项目/章节上下文）
//   2. 计算消息预算 = max_context_tokens - system_prompt_tokens
//   3. 从最新消息反向截断到预算上限
//   4. 统计并返回
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int max_context_tokens,
    const Project* project,
    const std::string& chapter_id)
{
    ContextAssembly result;

    // 1. 构建 system prompt
    if (project) {
        result.system_prompt = buildSystemPrompt(*project, chapter_id);
    }

    // 2. 计算消息预算
    int sys_tokens = result.system_prompt.empty()
        ? 0 : llm::TokenCounter::countTokens(result.system_prompt);
    int msg_budget = std::max(0, max_context_tokens - sys_tokens);

    // 3. 截断消息（从最新消息反向保留，丢弃最旧的）
    const auto& all_msgs = conversation.messages();
    result.messages = truncateMessages(all_msgs, msg_budget, result.truncated_count);

    if (result.truncated_count > 0) {
        spdlog::info("[ContextManager] 截断 {} 条消息 (预算={} sys={} msg_budget={})",
                     result.truncated_count, max_context_tokens, sys_tokens, msg_budget);
    }

    // 4. 统计总 token
    int msg_tokens = llm::TokenCounter::countMessages(result.messages);
    result.total_tokens = sys_tokens + msg_tokens;
    return result;
}

// ===========================================================================
// buildSystemPrompt
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project,
    const std::string& chapter_id)
{
    if (chapter_id.empty()) {
        std::string prompt;
        prompt += "# 项目: " + project.title + "\n";
        if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
        if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";
        return prompt;
    }

    prompt::PromptContextOptions options;
    options.task = "write_chapter";
    options.chapter_id = chapter_id;

    auto ctx = prompt::PromptContextBuilder::buildForChapter(project, options);
    if (!ctx) {
        spdlog::warn("[ContextManager] 章节 '{}' 上下文构建失败，回退", chapter_id);
        return buildSystemPrompt(project);
    }

    return ctx->rendered_prompt;
}

// ===========================================================================
// truncateMessages — 从最新消息反向贪心保留
// ===========================================================================

std::vector<llm::Message> ContextManager::truncateMessages(
    const std::vector<llm::Message>& messages,
    int budget,
    int& truncated_count)
{
    truncated_count = 0;
    if (messages.empty()) return messages;
    if (budget <= 0) {
        truncated_count = static_cast<int>(messages.size());
        return {};
    }
    if (llm::TokenCounter::countMessages(messages) <= budget) return messages;

    // 从最新消息反向遍历，贪心保留直到预算耗尽
    std::vector<llm::Message> result;
    int used = 0;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        int cost = llm::TokenCounter::countSingleMessage(*it);
        if (used + cost > budget) break;
        used += cost;
        result.push_back(*it);
    }
    std::reverse(result.begin(), result.end());
    truncated_count = static_cast<int>(messages.size()) - static_cast<int>(result.size());

    // 安全兜底：如果所有消息都太大，至少保留最后一条
    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }
    return result;
}

} // namespace agent
