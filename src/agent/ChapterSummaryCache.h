#pragma once

/// 章节摘要缓存 — 管理 .novelagent/summaries.json 的读写。
///
/// Phase 4 架构改进：从 ContextManager 拆分出独立职责。
/// 通过 IStorageBackend 访问存储，不直接依赖 ProjectIO。

#include "agent/ContextManagerTypes.h"
#include "project/IStorageBackend.h"

#include <map>
#include <optional>
#include <string>

namespace agent {

/// 章节摘要缓存管理器。
class ChapterSummaryCache {
public:
    /// 构造函数注入存储后端。
    explicit ChapterSummaryCache(IStorageBackend& storage)
        : storage_(storage) {}

    /// 从 summaries.json 读取单章摘要。
    std::optional<ChapterSummaryEntry> get(const std::string& chapter_id);

    /// 更新单章摘要并写回。
    void update(const ChapterSummaryEntry& entry);

    /// 加载所有章节摘要。
    std::map<std::string, ChapterSummaryEntry> loadAll();

private:
    IStorageBackend& storage_;

    /// summaries.json 在 .novelagent 子目录下的相对路径。
    static constexpr const char* kSummariesFile = "summaries.json";
};

} // namespace agent
