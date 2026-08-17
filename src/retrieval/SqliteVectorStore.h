#pragma once

// SqliteVectorStore — SQLite（sqlite-vec vec0）后端的向量存储。
//
// 实现 IVectorStore 抽象接口（替换旧 JSON 文件 VectorStore）。与
// ProjectIndexService 共享同一 SqliteStore——索引服务直连 SQL 写库，
// 本类只服务检索调用方（工具等），二者互不嵌套，避免重复加锁。
//
// 相似度契约：搜索返回 similarity ∈ [0,1] 降序（与旧实现一致）。
// sqlite-vec 返回 cosine distance ∈ [0,2]，换算 similarity = 1 - d/2。

#include "retrieval/IVectorStore.h"

#include <optional>
#include <string>
#include <vector>

namespace storage { class SqliteStore; }

namespace retrieval {

class SqliteVectorStore : public IVectorStore {
public:
    // @param store SQLite 单库；非拥有引用，调用方保证存活期覆盖本对象。
    explicit SqliteVectorStore(storage::SqliteStore& store) : store_(store) {}

    void insert(const std::string& id,
                const EmbeddingVector& embedding,
                const nlohmann::json& metadata) override;
    void insertBatch(const std::vector<VectorEntry>& entries) override;
    bool remove(const std::string& id) override;
    void update(const std::string& id, const EmbeddingVector& embedding) override;

    std::vector<SearchResult> search(const EmbeddingVector& query_embedding,
                                     int top_k = 10) const override;

    int count() const override;
    bool contains(const std::string& id) const override;

    // 事务即持久化：无需显式落盘。为兼容 IVectorStore 契约保留为空实现。
    void flush() override {}

    // 与旧 VectorStore 兼容的查询：按 id 返回条目副本；未命中返回 nullopt。
    std::optional<VectorEntry> get(const std::string& id) const;

private:
    storage::SqliteStore& store_;
};

} // namespace retrieval