#pragma once

/// 上下文管理器（精简版 — 移除过度设计的预算分配/降级/摘要系统）。
///
/// 核心职责：
///   1. 构建动态 system prompt（项目/章节上下文）
///   2. 按 token 预算截断对话历史（从最新消息反向保留）
///   3. 会话持久化委托给 SessionPersistence
///
/// 依赖：通过 IStorageBackend 抽象访问存储，不直接依赖 ProjectIO。

#include "agent/ContextManagerTypes.h"
#include "agent/SessionPersistence.h"
#include "llm/Conversation.h"

#include <string>
#include <vector>

// 前向声明
struct Project;
class IStorageBackend;

namespace agent {

/// 上下文管理器。
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

    /// 组装上下文 — 一站式入口（精简版）。
    ///
    /// 流程：
    ///   1. 构建 system prompt（项目/章节上下文，如果提供了 Project）
    ///   2. 计算消息预算 = max_context_tokens - system_prompt_tokens
    ///   3. 从最新消息反向截断到预算上限
    ///   4. 返回 ContextAssembly
    ContextAssembly assemble(
        const llm::Conversation& conversation,
        int max_context_tokens,
        const Project* project = nullptr,
        const std::string& chapter_id = "");

    /// 构建系统提示词（委托 PromptContextBuilder）。
    std::string buildSystemPrompt(const Project& project,
                                   const std::string& chapter_id = "");

    // ================================================================
    // 会话持久化（委托 SessionPersistence）
    // ================================================================

    SessionPersistence& persistence() { return persistence_; }

    void saveSession(const llm::Conversation& conv) { persistence_.save(conv); }
    llm::Conversation loadSession() { return persistence_.load(); }
    void archiveSession(const llm::Conversation& conv) { persistence_.archive(conv); }

private:
    IStorageBackend& storage_;
    SessionPersistence persistence_;

    /// 按 token 预算从新到旧截断消息（保留最新消息）。
    static std::vector<llm::Message> truncateMessages(
        const std::vector<llm::Message>& messages,
        int budget,
        int& truncated_count);
};

} // namespace agent
