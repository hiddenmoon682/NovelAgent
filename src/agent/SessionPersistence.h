#pragma once

/// 会话持久化 — 对话历史 + 会话元数据的保存/加载/归档。
///
/// Phase 4 架构改进：从 ContextManager 拆分出独立职责。
/// 通过 IStorageBackend 访问存储，不直接依赖 ProjectIO。

#include "agent/ContextManagerTypes.h"
#include "project/IStorageBackend.h"

#include <string>
#include <vector>

namespace llm {
class Conversation;
} // namespace llm

namespace agent {

/// 会话元数据（跨重启恢复的状态）。
struct SessionMeta {
    std::string compacted_summary;
    int compaction_marker = 0;
    SessionTokenState token_state;
    std::string last_chapter_id;
    std::vector<size_t> preserved_indices;
    int64_t project_mtime = 0;  // Project 最后修改时间戳（用于检测设定变更）
};

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

    // ── 会话元数据（跨重启恢复）──

    /// 保存会话元数据到 .novelagent/session_meta.json。
    void saveMeta(const SessionMeta& meta);

    /// 加载会话元数据，如果文件不存在返回默认值。
    SessionMeta loadMeta() const;

private:
    IStorageBackend& storage_;

    static constexpr const char* kConversationFile = "conversation.json";
    static constexpr const char* kSessionMetaFile = "session_meta.json";
    static constexpr const char* kArchiveDir = "archive";
};

} // namespace agent
