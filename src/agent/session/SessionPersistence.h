#pragma once

// 会话持久化 — 对话历史 + 会话元数据的保存/加载/归档。
//
// Phase 4 架构改进：从 ContextManager 拆分出独立职责。
// 通过 FileStorageBackend 访问存储（封装项目路径 + 转发 ProjectIO）。

#include "agent/context/ContextManagerTypes.h"
#include "project/FileStorageBackend.h"

#include <string>
#include <vector>

namespace llm {
class IMemory;
class Memory;
} // namespace llm

namespace agent {

// 会话元数据（跨重启恢复的状态）。
//
// 与 conversation.json（完整对话历史）配对存储为 session_meta.json，
// 使 /exit 后重启可以无缝恢复上下文管理器的内部状态（摘要、token 统计等）。
struct SessionMeta {
    std::string compacted_summary;          //  LLM 生成的压缩摘要文本（空 = 无）
    int compaction_marker = 0;              //  被 compact() 压缩的消息数，/rewind 跨边界检测用
    SessionTokenState token_state;          //  累计 token 消耗统计（输入/输出/请求数）
    std::vector<size_t> preserved_indices;  //  /pin 保留的消息在 conversation 中的索引
    int64_t project_mtime = 0;              //  novel.json 最后修改时间戳（检测设定变更）
};

// 会话持久化管理器。
//
// 两份持久化文件（均位于 .novelagent/ 下）：
//   - conversation.json → save()/load()，完整对话历史（Messages JSON 数组）
//   - session_meta.json  → saveMeta()/loadMeta()，轻量元数据（摘要/token/章节等）
//
// 分离设计的好处：元数据很小（<1KB），启动时快速读取恢复上下文状态；
// 对话历史可能很大（>100KB），按需加载。
class SessionPersistence {
public:
    // 构造函数注入存储后端。
    explicit SessionPersistence(FileStorageBackend& storage)
        : storage_(storage) {}

    // 保存完整对话历史到 .novelagent/conversation.json。
    void save(const llm::IMemory& memory);

    // 从 .novelagent/conversation.json 加载对话历史。
    llm::Memory load();

    // 归档当前对话到 .novelagent/archive/conversation_<timestamp>.json。
    void archive(const llm::IMemory& memory);

    // ── 会话元数据（跨重启恢复）──

    // 保存会话元数据到 .novelagent/session_meta.json。
    void saveMeta(const SessionMeta& meta);

    // 加载会话元数据，如果文件不存在返回默认值。
    SessionMeta loadMeta() const;

private:
    FileStorageBackend& storage_;

    static constexpr const char* kConversationFile = "conversation.json";
    static constexpr const char* kSessionMetaFile = "session_meta.json";
    static constexpr const char* kArchiveDir = "archive";
};

} // namespace agent
