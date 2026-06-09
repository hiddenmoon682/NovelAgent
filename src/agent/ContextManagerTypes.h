#pragma once

/// ContextManager 相关类型定义 — 拆分后各子模块共享的 DTO。
/// 从 ContextManager.h 提取，避免拆分后的类之间产生循环依赖。

#include "llm/Message.h"

#include <string>
#include <vector>

namespace agent {

/// 上下文组装结果。
struct ContextAssembly {
    std::vector<llm::Message> messages;
    std::string system_prompt;
    int total_tokens = 0;
    int budget = 0;
    bool truncated = false;
    int truncated_count = 0;
    int degradation_level = 0;
};

/// 对话摘要 — 从对话历史中规则提取的关键信息。
struct ConversationSummary {
    std::string summary;
    std::vector<std::string> character_names;
    std::vector<std::string> chapter_refs;
    std::vector<std::string> plot_points;
    std::vector<std::string> tasks;
    int source_message_count = 0;
};

/// 章节摘要缓存条目。
struct ChapterSummaryEntry {
    std::string chapter_id;
    std::string summary;
    std::vector<std::string> characters;
    std::vector<std::string> settings;
    std::vector<std::string> key_events;
    std::string updated_at;
};

/// 降级等级。
enum class DegradationLevel {
    None = 0,
    TruncateChapter = 1,
    RemoveDetails = 2,
    RemoveAdjacent = 3,
    TruncateConv = 4,
    Summarize = 5
};

/// 预算分配详情（50/30/20 规则）。
struct BudgetAllocation {
    int total_budget = 0;
    int chapter_budget = 0;
    int conversation_budget = 0;
    int summary_budget = 0;
    int degradation_level = 0;
};

} // namespace agent
