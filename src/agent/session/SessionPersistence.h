#pragma once

// 会话持久化 — 对话历史的保存/加载/归档。
//
// Phase 4 架构改进：从 ContextManager 拆分出独立职责。
// 通过 FileStorageBackend 访问存储（封装项目路径 + 转发 ProjectIO）。
//
// 持久化文件（位于 .novelagent/ 下）：
//   - conversation.json           → save()/load()，完整对话历史（Messages JSON 数组）
//   - archive/conversation_<ts>.json → archive()，新建会话前归档旧对话
//
// 设计要点：
//   - system prompt 不持久化——每次启动由 NovelAgentApp 重新组装
//     （技能/工具存根可能变化，落盘的旧 prompt 会覆盖新组装结果）。
//   - preserved（/pin）标记随消息一同持久化，跨重启保留。
//   - 压缩摘要以普通消息形式存在于对话历史中，随 conversation.json 自然恢复，
//     无需独立的元数据文件（原 session_meta.json 方案无消费者，已按 YAGNI 移除）。

#include "project/FileStorageBackend.h"

#include <string>

namespace llm {
class IMemory;
class Memory;
} // namespace llm

namespace agent {

// 会话持久化管理器。
class SessionPersistence {
public:
    // 构造函数注入存储后端。
    explicit SessionPersistence(FileStorageBackend& storage)
        : storage_(storage) {}

    // 保存完整对话历史到 .novelagent/conversation.json（全量覆盖，原子写）。
    void save(const llm::IMemory& memory);

    // 从 .novelagent/conversation.json 加载对话历史（不含 system prompt）。
    llm::Memory load();

    // 归档当前对话到 .novelagent/archive/conversation_<timestamp>.json。
    void archive(const llm::IMemory& memory);

private:
    FileStorageBackend& storage_;

    static constexpr const char* kConversationFile = "conversation.json";
    static constexpr const char* kArchiveDir = "archive";
};

} // namespace agent
