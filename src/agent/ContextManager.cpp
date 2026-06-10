/// ContextManager 实现 — 编排器（Phase 4 架构改进版）。
///
/// 职责：组合子模块，提供统一的 assemble() 入口。
/// 所有具体逻辑委托给：
///   - ConversationSummarizer（对话摘要）
///   - ChapterSummaryCache（章节缓存）
///   - DegradationPipeline（降级策略）
///   - SessionPersistence（会话持久化）

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

namespace {
constexpr double kChapterRatio = 0.50;
constexpr double kConversationRatio = 0.30;
constexpr double kSummaryRatio = 0.20;
constexpr int kMinConversationTurns = 5;
} // namespace

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
    , summary_cache_(storage_)
    , persistence_(storage_)
{
    degradation_.registerDefaultStrategies();
}

ContextManager::ContextManager(IStorageBackend& storage)
    : storage_(storage)
    , summary_cache_(storage)
    , persistence_(storage)
{
    degradation_.registerDefaultStrategies();
}

void ContextManager::setSummaryKeywords(const SummaryKeywords& kw)
{
    summarizer_.setKeywords(kw);
}

// ===========================================================================
// assemble — 一站式入口
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int context_window,
    const Project* project,
    const std::string& chapter_id)
{
    ContextAssembly result;

    // 1. 预算分配
    BudgetAllocation alloc = allocateBudget(context_window);
    result.budget = alloc.total_budget;

    // 2. 构建系统提示词
    if (project) {
        result.system_prompt = buildSystemPrompt(*project, chapter_id);
    }

    // 3. 计算 token 开销
    int sys_tokens = result.system_prompt.empty()
        ? 0 : llm::TokenCounter::countTokens(result.system_prompt);
    int msg_budget = std::max(0, alloc.total_budget - sys_tokens);

    const auto& all_msgs = conversation.messages();
    int raw_msg_tokens = llm::TokenCounter::countMessages(all_msgs);

    // 4. 触发降级
    if (raw_msg_tokens > msg_budget || sys_tokens > alloc.chapter_budget) {
        DegradationLevel level = degradation_.determineLevel(
            sys_tokens + raw_msg_tokens, alloc.total_budget);

        if (level != DegradationLevel::None) {
            result.system_prompt = degradation_.execute(result.system_prompt, level);
            sys_tokens = result.system_prompt.empty()
                ? 0 : llm::TokenCounter::countTokens(result.system_prompt);
            msg_budget = std::max(0, alloc.total_budget - sys_tokens);
            result.degradation_level = static_cast<int>(level);

            spdlog::info("[ContextManager] 降级 L{} — sys={} msg_budget={}",
                         static_cast<int>(level), sys_tokens, msg_budget);
        }
    }

    // 5. 对话摘要
    std::string summary_text;
    if (raw_msg_tokens > msg_budget && all_msgs.size() > kMinConversationTurns * 2) {
        ConversationSummary summary = summarizer_.summarize(all_msgs);
        summary_text = ConversationSummarizer::render(summary);
        int summary_tokens = llm::TokenCounter::countTokens(summary_text);
        msg_budget = std::max(0, msg_budget - summary_tokens);
    }

    // 6. 截断消息
    result.messages = truncateMessages(all_msgs, msg_budget, result.truncated_count);
    result.truncated = (result.truncated_count > 0);

    // 7. 注入摘要
    if (!summary_text.empty() && !result.messages.empty()) {
        llm::Message context_note = llm::Message::user(
            "[上下文摘要 — 之前对话的关键信息]\n" + summary_text);
        result.messages.insert(result.messages.begin(), context_note);
    }

    if (result.truncated) {
        spdlog::info("[ContextManager] 截断 {} 条 (预算={} sys={} msg={} L={})",
                     result.truncated_count, result.budget, sys_tokens, msg_budget,
                     result.degradation_level);
    }

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
// 预算
// ===========================================================================

int ContextManager::calculateBudget(int context_window)
{
    return static_cast<int>(context_window * 0.8);
}

BudgetAllocation ContextManager::allocateBudget(int context_window) const
{
    BudgetAllocation alloc;
    alloc.total_budget = calculateBudget(context_window);
    alloc.chapter_budget = static_cast<int>(alloc.total_budget * kChapterRatio);
    alloc.conversation_budget = static_cast<int>(alloc.total_budget * kConversationRatio);
    alloc.summary_budget = static_cast<int>(alloc.total_budget * kSummaryRatio);
    return alloc;
}

// ===========================================================================
// truncateMessages
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

    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }
    return result;
}

} // namespace agent
