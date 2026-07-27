#pragma once

// LongTermMemoryStore — 长期记忆日志（journal），跨会话持久化的事实源。
//
// 设计原则：日志为事实源，向量库只是派生索引。
//   - 记忆条目（事实/偏好/事件/压缩摘要）以 JSON 日志形式持久化；
//   - 向量索引由 ProjectIndexService 从日志派生（增量、可随时重建）；
//   - 换嵌入模型或索引损坏时，记忆本身不丢失。
//
// 写入来源：
//   1. save_memory 工具 — LLM 主动记录持久事实
//   2. Agent 压缩回调 — 会话压缩摘要自动沉淀
//
// 持久化位置：<project>/.novelagent/memories.json
// 线程安全：所有公开方法内部加锁，支持工作线程写入 + 索引线程读取。

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace agent {

// 单条长期记忆。
struct MemoryEntry {
    std::string id;           // 唯一标识，如 "mem-1721980800-3"
    std::string text;         // 记忆内容
    std::string kind;         // 类型: fact / preference / event / summary
    int64_t created_at = 0;   // 创建时间（epoch 秒）
};

class LongTermMemoryStore {
public:
    // 初始化并从文件加载日志；文件不存在时从空开始。
    void init(const std::string& path);

    // 追加一条记忆并立即持久化。返回生成的条目 id；未初始化时返回空串。
    std::string append(const std::string& text, const std::string& kind);

    // 删除指定 id 的记忆并立即持久化。
    bool remove(const std::string& id);

    // 全部记忆条目的副本（线程安全）。
    std::vector<MemoryEntry> entries() const;

    size_t count() const;
    bool initialized() const;

private:
    void loadFromFile();
    void saveToFile() const;  // 调用方需持有锁

    std::string path_;
    std::vector<MemoryEntry> entries_;
    int seq_ = 0;             // 同秒内追加的去重序号
    bool initialized_ = false;
    mutable std::mutex mutex_;
};

} // namespace agent
