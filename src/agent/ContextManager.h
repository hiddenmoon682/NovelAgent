#pragma once

#include "llm/Message.h"

#include <string>
#include <vector>
#include <map>

// 前向声明（避免循环依赖）
struct Project;

namespace llm {
class Conversation;
}

namespace agent {

/// 上下文组装结果 — 包含截断后的消息、系统提示词和预算信息。
struct ContextAssembly {
    std::vector<llm::Message> messages;  // 截断后的消息列表
    std::string system_prompt;           // 系统提示词（供 LLMClient 使用）
    int total_tokens = 0;                // 占用的 token 数（TokenCounter 启发式估算，非精确值）
    int budget = 0;                      // 上下文窗口预算上限
    bool truncated = false;              // 是否发生了消息截断
    int truncated_count = 0;             // 被移除的消息条数
    int degradation_level = 0;           // 当前降级等级（0=未降级, 1-5=各级降级）
};

/// 对话摘要 — 从对话历史中规则提取的关键信息。
struct ConversationSummary {
    std::string summary;                     // 摘要文本
    std::vector<std::string> character_names; // 提及的角色名
    std::vector<std::string> chapter_refs;    // 提及的章节 ID
    std::vector<std::string> plot_points;     // 关键剧情点
    std::vector<std::string> tasks;           // 当前进行中的任务
    int source_message_count = 0;             // 摘要覆盖的原始消息数
};

/// 章节摘要缓存条目。
struct ChapterSummaryEntry {
    std::string chapter_id;
    std::string summary;                     // 章节摘要（关键事件）
    std::vector<std::string> characters;     // 出场角色
    std::vector<std::string> settings;       // 场景变化
    std::vector<std::string> key_events;     // 关键事件
    std::string updated_at;                  // 更新时间戳
};

/// 降级等级 — 从轻到重逐级压缩上下文。
enum class DegradationLevel {
    None = 0,           // 未降级，使用完整预算分配
    TruncateChapter = 1,// 截断当前章节到末尾 2000 字
    RemoveDetails = 2,  // 移除角色详细档案（保留名称和角色类型）
    RemoveAdjacent = 3, // 移除相邻章节大纲
    TruncateConv = 4,   // 截断对话到最近 5 轮
    Summarize = 5       // 全文压缩为摘要
};

/// 预算分配详情（50/30/20 规则）。
struct BudgetAllocation {
    int total_budget = 0;        // 总输入预算
    int chapter_budget = 0;      // 50% — 当前章节 + 大纲 + 角色
    int conversation_budget = 0; // 30% — 最近对话
    int summary_budget = 0;      // 20% — 历史摘要
    int degradation_level = 0;   // 当前降级等级
};

/// 上下文管理器（Phase 4 完整版）。
///
/// 职责：
/// - 构建系统提示词（委托 PromptContextBuilder）
/// - 计算 token 预算（50/30/20 动态分配）
/// - 按预算截断对话历史（从旧到新移除超出预算的消息）
/// - 对话历史摘要（规则提取，不调用 LLM）
/// - 章节摘要缓存管理
/// - 多级降级策略（5 级）
/// - 会话持久化（自动保存/加载）
class ContextManager {
public:
    ContextManager() = default;

    // ================================================================
    // 核心入口
    // ================================================================

    /// 组装上下文 — 一站式入口（Phase 4 完整版）。
    ///
    /// 按 50/30/20 规则分配预算，必要时触发多级降级。
    ///
    /// @param conversation   当前对话历史
    /// @param context_window 模型上下文窗口大小（如 65536）
    /// @param project        可选项目指针（有则构建系统提示词）
    /// @param chapter_id     可选章节 ID（用于 buildSystemPrompt 的上下文筛选）
    /// @return               ContextAssembly（截断后的消息 + 预算信息）
    ContextAssembly assemble(
        const llm::Conversation& conversation,
        int context_window,
        const Project* project = nullptr,
        const std::string& chapter_id = "");

    /// 构建系统提示词（委托给 PromptContextBuilder）。
    std::string buildSystemPrompt(const Project& project,
                                   const std::string& chapter_id = "");

    /// 计算可用的 token 预算（80/20 规则）。
    static int calculateBudget(int context_window);

    // ================================================================
    // 预算分配（Phase 4 新增）
    // ================================================================

    /// 按 50/30/20 规则分配预算。
    /// - 50%：当前章节全文 + 大纲 + 场景角色信息
    /// - 30%：最近对话（原始消息）
    /// - 20%：历史压缩摘要
    BudgetAllocation allocateBudget(int context_window) const;

    // ================================================================
    // 对话摘要（Phase 4.1 新增）
    // ================================================================

    /// 从对话历史中规则提取关键信息，不调用 LLM。
    ///
    /// 策略：保留角色名、章节引用、剧情要点、当前任务，
    /// 去掉闲聊和技术细节。
    ///
    /// @param messages  待摘要的消息列表
    /// @return          结构化摘要信息
    static ConversationSummary summarizeConversation(
        const std::vector<llm::Message>& messages);

    /// 将结构化摘要渲染为可注入上下文的文本。
    static std::string renderSummary(const ConversationSummary& summary);

    // ================================================================
    // 章节摘要缓存（Phase 4.2 新增）
    // ================================================================

    /// 从 .novelagent/summaries.json 读取章节摘要。
    /// @param project_path 项目根目录路径
    /// @param chapter_id   章节 ID
    /// @return             章节摘要（不存在时返回 nullopt）
    static std::optional<ChapterSummaryEntry> getChapterSummary(
        const std::string& project_path,
        const std::string& chapter_id);

    /// 更新章节摘要并写回 .novelagent/summaries.json。
    /// @param project_path 项目根目录路径
    /// @param entry        章节摘要条目
    static void updateChapterSummary(
        const std::string& project_path,
        const ChapterSummaryEntry& entry);

    /// 加载所有章节摘要。
    static std::map<std::string, ChapterSummaryEntry> loadAllSummaries(
        const std::string& project_path);

    // ================================================================
    // 多级降级（Phase 4.3 新增）
    // ================================================================

    /// 按降级等级压缩系统提示词。
    /// @param system_prompt  原始系统提示词
    /// @param level          降级等级
    /// @return               压缩后的系统提示词
    static std::string applyDegradation(
        const std::string& system_prompt,
        DegradationLevel level);

    /// 确定需要触发的降级等级。
    /// 从 None 开始，逐级尝试，直到预算够用。
    static DegradationLevel determineDegradation(
        int required_tokens,
        int available_budget);

    // ================================================================
    // 会话持久化（Phase 4.4 新增）
    // ================================================================

    /// 保存完整对话历史到 .novelagent/conversation.json。
    /// @param project_path 项目根目录路径
    /// @param conversation 对话历史
    static void saveSession(
        const std::string& project_path,
        const llm::Conversation& conversation);

    /// 从 .novelagent/conversation.json 加载对话历史。
    /// @param project_path 项目根目录路径
    /// @return             恢复的对话历史（文件不存在时为空 Conversation）
    static llm::Conversation loadSession(const std::string& project_path);

    /// 归档当前对话并清空（/clear 命令触发）。
    /// 旧对话保存到 .novelagent/archive/conversation_<timestamp>.json。
    /// @param project_path 项目根目录路径
    /// @param conversation 当前对话历史
    static void archiveSession(
        const std::string& project_path,
        const llm::Conversation& conversation);

private:
    /// 按 token 预算从旧到新截断消息。
    static std::vector<llm::Message> truncateMessages(
        const std::vector<llm::Message>& messages,
        int budget,
        int& truncated_count);

    /// 从消息中提取中文/英文角色名引用。
    static std::vector<std::string> extractCharacterNames(
        const std::vector<llm::Message>& messages);

    /// 从消息中提取章节 ID 引用（如 ch-001, 第一章 等）。
    static std::vector<std::string> extractChapterRefs(
        const std::vector<llm::Message>& messages);

    /// 从消息中提取剧情要点（含"剧情""情节""冲突""转折"等关键词的句子）。
    static std::vector<std::string> extractPlotPoints(
        const std::vector<llm::Message>& messages);

    /// 从消息中提取当前任务（用户最近提出的指令性语句）。
    static std::vector<std::string> extractTasks(
        const std::vector<llm::Message>& messages);

    /// 对单条消息做简单分句（按中英文标点切分）。
    static std::vector<std::string> splitSentences(const std::string& text);
};

} // namespace agent
