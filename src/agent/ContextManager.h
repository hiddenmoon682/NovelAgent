#pragma once

/// 上下文管理器（Phase 4 架构改进版）。
///
/// 编排器角色：组合 ConversationSummarizer / ChapterSummaryCache /
/// DegradationPipeline / SessionPersistence 四个子模块，
/// 对外提供统一的 assemble() 入口。
///
/// 依赖：通过 IStorageBackend 抽象访问存储，不直接依赖 ProjectIO。

#include "agent/ContextManagerTypes.h"
#include "agent/ConversationSummarizer.h"
#include "agent/ChapterSummaryCache.h"
#include "agent/DegradationPipeline.h"
#include "agent/SessionPersistence.h"
#include "llm/Conversation.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

// 前向声明
struct Project;
class IStorageBackend;

namespace llm {
class Conversation;
}

namespace agent {

/// 上下文管理器 — 编排器。
class ContextManager {
public:
    /// 默认构造函数（不使用持久化存储）。
    ContextManager();

    /// 构造函数注入存储后端。
    /// @param storage 存储后端（FileStorageBackend 或测试 Mock）
    explicit ContextManager(IStorageBackend& storage);

    // ================================================================
    // 核心入口
    // ================================================================

    /// 组装上下文 — 一站式入口。
    ContextAssembly assemble(
        const llm::Conversation& conversation,
        int context_window,
        const Project* project = nullptr,
        const std::string& chapter_id = "");

    /// 构建系统提示词（委托 PromptContextBuilder）。
    std::string buildSystemPrompt(const Project& project,
                                   const std::string& chapter_id = "");

    /// 计算可用的 token 预算（80/20 规则）。
    static int calculateBudget(int context_window);

    /// 按 50/30/20 规则分配预算。
    BudgetAllocation allocateBudget(int context_window) const;

    // ================================================================
    // 子模块访问（供高级用法和测试）
    // ================================================================

    ConversationSummarizer& summarizer() { return summarizer_; }
    ChapterSummaryCache& summaryCache() { return summary_cache_; }
    DegradationPipeline& degradation() { return degradation_; }
    SessionPersistence& persistence() { return persistence_; }

    /// 更新摘要关键词（P3 — 可配置化）。
    void setSummaryKeywords(const SummaryKeywords& kw);

    // ================================================================
    // 向后兼容：委托给子模块（避免大规模测试修改）
    // ================================================================

    /// 委托 ConversationSummarizer::summarize
    ConversationSummary summarizeConversation(
        const std::vector<llm::Message>& messages) const {
        return summarizer_.summarize(messages);
    }
    static std::string renderSummary(const ConversationSummary& s) {
        return ConversationSummarizer::render(s);
    }

    /// 委托 ChapterSummaryCache
    std::optional<ChapterSummaryEntry> getChapterSummary(const std::string& chapter_id) {
        return summary_cache_.get(chapter_id);
    }
    void updateChapterSummary(const ChapterSummaryEntry& entry) {
        summary_cache_.update(entry);
    }
    std::map<std::string, ChapterSummaryEntry> loadAllSummaries() {
        return summary_cache_.loadAll();
    }

    /// 委托 DegradationPipeline
    DegradationLevel determineDegradation(int required, int budget) const {
        return degradation_.determineLevel(required, budget);
    }
    std::string applyDegradation(const std::string& prompt, DegradationLevel level) const {
        return degradation_.execute(prompt, level);
    }

    /// 委托 SessionPersistence
    void saveSession(const llm::Conversation& conv) { persistence_.save(conv); }
    llm::Conversation loadSession() { return persistence_.load(); }
    void archiveSession(const llm::Conversation& conv) { persistence_.archive(conv); }

private:
    IStorageBackend& storage_;

    // Phase 4 拆分出的子模块（组合，非继承）
    ConversationSummarizer summarizer_;
    ChapterSummaryCache summary_cache_;
    DegradationPipeline degradation_;
    SessionPersistence persistence_;

    /// 按 token 预算从旧到新截断消息。
    static std::vector<llm::Message> truncateMessages(
        const std::vector<llm::Message>& messages,
        int budget,
        int& truncated_count);
};

} // namespace agent
