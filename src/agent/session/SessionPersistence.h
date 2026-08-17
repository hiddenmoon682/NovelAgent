#pragma once

// 会话持久化 — 多会话的保存/加载/切换/删除（SQLite 实现）。
//
// 持久化布局（novel.db，见 SqliteStore::ensureSchema）：
//   - sessions 表          → 会话元信息（archived=1 为归档态：数据保留、列表不可见）
//   - messages 表          → 快照层（save() 事务内 DELETE + 重插，对应原 <id>.json）
//   - message_history 表   → 完整历史层（append-only，对应原 <id>.history，
//                            appendHistory 事务内从 MAX(seq)+1 起连续编号）
//
// 设计要点（与原文件版保持一致）：
//   - system prompt 不持久化——每次启动由 NovelAgentApp 重新组装。
//   - preserved（/pin）标记随消息一同持久化，跨重启保留。
//   - 压缩摘要以普通消息形式存在于快照层，随会话自然恢复。
//   - 会话标题在首次保存时从首条 user 消息自动提取（UTF-8 安全截断）。
//
// 线程安全：全部操作经 SqliteStore 的全库锁串行化（原 index_mutex_ 删除）。

#include "project/FileStorageBackend.h"

#include <string>
#include <vector>

namespace storage { class SqliteStore; }
namespace llm {
class IMemory;
class Memory;
struct Message;
} // namespace llm

namespace agent {

// 会话元信息（sessions 表一行）。
struct SessionInfo {
    std::string id;
    std::string title;       // 空 = 尚无用户消息（前端显示"新会话"）
    std::string created_at;  // ISO 8601 UTC
    std::string updated_at;  // ISO 8601 UTC
};

// 会话持久化管理器（SQLite 实现，公开接口与原文件版一致）。
class SessionPersistence {
public:
    // @param sqlite  SQLite 单库（非拥有引用；调用方保证存活期覆盖本对象）。
    // @param storage 文件存储后端（仅用于 nowTimestamp 时间戳）。
    SessionPersistence(storage::SqliteStore& sqlite, FileStorageBackend& storage)
        : sqlite_(sqlite), storage_(storage) {}

    // ── 按显式 session_id 读写 ──

    // 保存完整对话历史（快照层全量覆盖，事务内 DELETE + 重插）；
    // 同时刷新 updated_at，并在标题为空时从首条 user 消息自动提取标题。
    void save(const std::string& session_id, const llm::IMemory& memory);

    // 加载指定会话快照（不含 system prompt）；不存在返回空 Memory。
    llm::Memory load(const std::string& session_id);

    // 追加被压缩消息到完整历史层（append-only，事务内续号）。
    void appendHistory(const std::string& session_id,
                       const std::vector<llm::Message>& messages);

    // 读取完整历史层全部消息（按 seq 升序）。
    std::vector<llm::Message> loadHistory(const std::string& session_id);

    // ── 会话管理 ──

    // 会话列表（不含归档，按 updated_at 降序，最近使用在前）。
    std::vector<SessionInfo> listSessions();

    // 新建空会话，返回新会话 id（s-<时间戳> 格式）。
    std::string createSession();

    // 删除指定会话：置 sessions.archived=1（数据保留、列表不可见）。
    bool deleteSession(const std::string& id);

private:
    // 由时间戳生成会话 id；sessions 表（含归档）已存在时追加序号。
    // 须在 sqlite_ 锁内调用。
    std::string makeSessionId(const std::string& timestamp) const;

    storage::SqliteStore& sqlite_;
    FileStorageBackend& storage_;
};

} // namespace agent