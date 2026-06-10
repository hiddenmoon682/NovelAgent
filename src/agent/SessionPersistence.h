#pragma once

/// 会话持久化 — 对话历史的保存/加载/归档。
///
/// Phase 4 架构改进：从 ContextManager 拆分出独立职责。
/// 通过 IStorageBackend 访问存储，不直接依赖 ProjectIO。

#include "project/IStorageBackend.h"

#include <string>

namespace llm {
class Conversation;
} // namespace llm

namespace agent {

/// 会话持久化管理器。
class SessionPersistence {
public:
    /// 构造函数注入存储后端。
    explicit SessionPersistence(IStorageBackend& storage)
        : storage_(storage) {}

    /// 保存完整对话历史到 .novelagent/conversation.json。
    void save(const llm::Conversation& conversation);

    /// 从 .novelagent/conversation.json 加载对话历史。
    llm::Conversation load();

    /// 归档当前对话到 .novelagent/archive/conversation_<timestamp>.json。
    void archive(const llm::Conversation& conversation);

private:
    IStorageBackend& storage_;

    static constexpr const char* kConversationFile = "conversation.json";
    static constexpr const char* kArchiveDir = "archive";
};

} // namespace agent
