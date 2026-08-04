#pragma once

// 会话持久化 — 多会话的保存/加载/切换/删除。
//
// 持久化布局（位于 .novelagent/ 下）：
//   - sessions/index.json   → 会话索引 {sessions: [{id,title,created_at,updated_at}]}（D3：无 active）
//   - sessions/<id>.json    → 单个会话的上下文快照（全量覆盖，用于恢复会话）
//   - sessions/<id>.history → 单个会话的完整历史（append-only，压缩时追加被压缩消息原文）
//   - archive/<id>.json     → deleteSession() 归档的已删会话快照（可手工恢复）
//   - archive/<id>.history  → deleteSession() 归档的已删会话完整历史（可手工恢复）
//
// 设计要点：
//   - system prompt 不持久化——每次启动由 NovelAgentApp 重新组装
//     （技能/工具存根可能变化，落盘的旧 prompt 会覆盖新组装结果）。
//   - preserved（/pin）标记随消息一同持久化，跨重启保留。
//   - 压缩摘要以普通消息形式存在于上下文快照中，随会话文件自然恢复。
//   - 会话标题在首次保存时从首条 user 消息自动提取（UTF-8 安全截断）。
//
// 双层持久化：
//   - 上下文快照层（<id>.json）保存恢复 LLM 上下文所需的状态（近期消息 + 摘要占位），
//     全量覆盖，体现“上次退出时的上下文状态”。
//   - 完整历史层（<id>.history）保存该会话的完整历史对话原文，append-only 追加、
//     永不删除，体现“完整历史对话”。压缩会从内存/快照中移除旧消息，但原文经
//     appendHistory 追加到历史层而不会永久丢失。

#include "project/FileStorageBackend.h"

#include <nlohmann/json_fwd.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace llm {
class IMemory;
class Memory;
struct Message;  // 用于完整历史层的 append/load 声明（见 llm/Message.h）
} // namespace llm

namespace agent {

// 会话元信息（index.json 中的一条记录）。
struct SessionInfo {
    std::string id;
    std::string title;       // 空 = 尚无用户消息（前端显示"新会话"）
    std::string created_at;  // ISO 8601 UTC
    std::string updated_at;  // ISO 8601 UTC
};

// 会话持久化管理器。所有方法按需读取 index.json（低频路径，无内存缓存）。
class SessionPersistence {
public:
    // 构造函数注入存储后端。
    explicit SessionPersistence(FileStorageBackend& storage)
        : storage_(storage) {}

    // ── 按显式 session_id 读写（D3：不依赖 active 字段）──

    // 保存完整对话历史到指定会话文件（全量覆盖，原子写）；
    // 同时刷新 updated_at，并在标题为空时从首条 user 消息自动提取标题。
    // 会话不存在时创建对应会话文件并登记到 index。
    void save(const std::string& session_id, const llm::IMemory& memory);

    // 加载指定会话的对话历史（不含 system prompt）；文件不存在返回空 Memory。
    llm::Memory load(const std::string& session_id);

    // 追加被压缩消息到指定会话的完整历史层（append-only，原子写）。
    // 压缩时被压缩掉的旧消息原文经此方法归档，避免因压缩而永久丢失。
    // 历史文件不存在时自动创建；调用方保证 session 存在。
    void appendHistory(const std::string& session_id,
                       const std::vector<llm::Message>& messages);

    // 读取指定会话的完整历史层（append-only 追加产生的全部消息，按时间顺序）。
    // 文件不存在或为空时返回空数组。
    std::vector<llm::Message> loadHistory(const std::string& session_id);

    // ── 会话管理 ──

    // 会话列表（按 updated_at 降序，最近使用的在前）。
    std::vector<SessionInfo> listSessions();

    // 新建空会话，返回新会话 id。
    std::string createSession();

    // 删除指定会话：上下文快照（archive/<id>.json）与完整历史（archive/<id>.history）
    // 一并归档，再从索引移除。
    bool deleteSession(const std::string& id);

private:
    // ── 索引读写与自愈 ──

    // 读取索引；不存在或损坏时扫描 sessions/ 目录重建（绝不丢弃既有会话文件）。
    // 返回的索引保证：active 为字符串且存在于 sessions 列表，每条 entry 含非空字符串 id。
    nlohmann::json loadIndex();
    // 校验索引结构完整性（active 类型、entry 字段、active 引用完整性）。
    bool indexValid(const nlohmann::json& idx) const;
    // 扫描 sessions/ 目录重建索引；尽量从损坏索引中回收元数据。
    nlohmann::json rebuildIndexFromDisk(const nlohmann::json& damaged);
    void saveIndex(const nlohmann::json& idx);

    // ── 路径与 id 生成 ──

    std::string sessionsDir() const;
    std::string sessionFile(const std::string& id) const;
    // 指定会话的完整历史层文件路径（append-only 追加，见 appendHistory）。
    std::string historyFile(const std::string& id) const;
    // 由时间戳生成文件名安全的会话 id，同秒冲突时追加序号。
    std::string makeSessionId(const std::string& timestamp) const;

    FileStorageBackend& storage_;

    // 多会话并发安全（D3）：保护 index.json 读-改-写（save/createSession/deleteSession/
    // switchSession/activeSessionId 等）。会话文件按 id 隔离无需锁，仅索引需串行化。
    mutable std::mutex index_mutex_;

    static constexpr const char* kSessionsDir = "sessions";
    static constexpr const char* kIndexFile = "index.json";
    static constexpr const char* kArchiveDir = "archive";
};

} // namespace agent
