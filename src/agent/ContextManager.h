#pragma once

#include "llm/Message.h"

#include <string>
#include <vector>

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
};

/// 上下文管理器（基础版）。
///
/// 职责：
/// - 构建系统提示词（委托 PromptContextBuilder）
/// - 计算 token 预算（输出预留 20%）
/// - 按预算截断对话历史（从旧到新移除超出预算的消息）
///
/// Phase 4 将扩展：摘要压缩、语义检索结果注入、动态预算分配。
class ContextManager {
public:
    ContextManager() = default;

    /// 组装上下文 — 一站式入口。
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
    ///
    /// 以 chapter_id 为目标章节，调用 PromptContextBuilder::buildForChapter，
    /// 将其 rendered_prompt 作为 system prompt 返回。
    ///
    /// @param project    小说项目
    /// @param chapter_id 目标章节 ID（为空则不带章节上下文）
    /// @return           渲染后的系统提示词文本
    std::string buildSystemPrompt(const Project& project,
                                   const std::string& chapter_id = "");

    /// 计算可用的 token 预算。
    ///
    /// 规则：上下文窗口的 80% 用于输入（消息 + 系统提示词），
    /// 剩余 20% 留给模型输出。Phase 4 将引入更细粒度的预算分配。
    ///
    /// @param context_window  模型上下文窗口大小
    /// @return                可用于输入的最大 token 数
    static int calculateBudget(int context_window);

private:
    /// 按 token 预算从旧到新截断消息。
    ///
    /// 从 messages 头部开始移除，直到剩余消息的 token 数 ≤ budget。
    /// system 角色消息跳过（它们通过 system_prompt 参数单独传递）。
    ///
    /// @param messages         待截断的消息列表
    /// @param budget           消息 token 预算上限
    /// @param truncated_count  [出参] 被移除的消息数
    /// @return                 截断后的消息列表
    static std::vector<llm::Message> truncateMessages(
        const std::vector<llm::Message>& messages,
        int budget,
        int& truncated_count);
};

} // namespace agent
