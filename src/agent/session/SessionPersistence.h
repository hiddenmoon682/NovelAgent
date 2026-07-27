#pragma once

// 会话持久化 — 多会话的保存/加载/切换/删除。
//
// 持久化布局（位于 .novelagent/ 下）：
//   - sessions/index.json   → 会话索引 {active, sessions: [{id,title,created_at,updated_at}]}
//   - sessions/<id>.json    → 单个会话的完整消息数组
//   - archive/<id>.json     → deleteSession() 归档的已删会话（可手工恢复）
//
// 设计要点：
//   - system prompt 不持久化——每次启动由 NovelAgentApp 重新组装
//     （技能/工具存根可能变化，落盘的旧 prompt 会覆盖新组装结果）。
//   - preserved（/pin）标记随消息一同持久化，跨重启保留。
//   - 压缩摘要以普通消息形式存在于对话历史中，随会话文件自然恢复。
//   - 会话标题在首次保存时从首条 user 消息自动提取（UTF-8 安全截断）。

#include "project/FileStorageBackend.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace llm {
class IMemory;
class Memory;
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

    // ── 当前 active 会话的读写 ──

    // 保存完整对话历史到 active 会话文件（全量覆盖，原子写）；
    // 同时刷新 updated_at，并在标题为空时从首条 user 消息自动提取标题。
    void save(const llm::IMemory& memory);

    // 加载 active 会话的对话历史（不含 system prompt）。
    llm::Memory load();

    // ── 会话管理 ──

    // 会话列表（按 updated_at 降序，最近使用的在前）。
    std::vector<SessionInfo> listSessions();

    // 当前 active 会话 id。
    std::string activeSessionId();

    // 新建空会话并设为 active，返回新会话 id。
    std::string createSession();

    // 将指定会话设为 active；id 不存在返回 false。
    bool switchSession(const std::string& id);

    // 删除指定会话：非空内容归档到 archive/<id>.json，再从索引移除。
    // 删除的是 active 会话时自动切到最近更新的剩余会话（一个不剩则新建空会话）。
    bool deleteSession(const std::string& id);

private:
    // 读取索引；不存在时初始化为含单个空会话的索引。
    nlohmann::json loadIndex();
    void saveIndex(const nlohmann::json& idx);
    std::string sessionsDir() const;
    std::string sessionFile(const std::string& id) const;
    // 由时间戳生成文件名安全的会话 id，同秒冲突时追加序号。
    std::string makeSessionId(const std::string& timestamp) const;

    FileStorageBackend& storage_;

    static constexpr const char* kSessionsDir = "sessions";
    static constexpr const char* kIndexFile = "index.json";
    static constexpr const char* kArchiveDir = "archive";
};

} // namespace agent
