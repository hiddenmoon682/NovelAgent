#pragma once

// 向量存储与语义搜索模块。
//
// 职责：存储文本嵌入向量，支持余弦相似度 ANN 搜索。
//
// 当前实现：JSON 文件后端（暴力搜索），适用于万级向量。
// Phase 4 后续可替换为 sqlite-vec（只需修改 .cpp 内部实现，接口不变）。
//
// 使用流程：
//   1. VectorStore store;
//   2. store.init("path/to/vectors.json");
//   3. store.insert("ch-001-seg-0", embedding, {{"type", "chapter"}});
//   4. auto results = store.search(query_embedding, 5);
//
// 线程安全：search/count/contains/get 支持并发读；insert/remove/update/write 互斥写。
//           读操作使用 shared_lock，写操作使用 unique_lock，读写互斥。

#include "retrieval/IVectorStore.h"

#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace retrieval {

// 向量存储 — JSON 文件后端 + 暴力余弦相似度搜索。
//
// 实现 IVectorStore 抽象接口，API 兼容 sqlite-vec（后续替换只需修改 .cpp）。
class VectorStore : public IVectorStore {
public:
    VectorStore() = default;
    ~VectorStore() = default;

    // ── 生命周期 ──

    // 初始化存储，打开或创建向量文件。
    // db_path 向量文件的完整路径（如 .novelagent/vectors.json）
    void init(const std::string& db_path);

    // 关闭存储，自动保存未写入的变更。
    void close();

    // ── CRUD ──

    // 插入单个向量及其元数据。
    // 若 id 已存在则覆盖。
    void insert(const std::string& id,
                const std::vector<float>& embedding,
                const nlohmann::json& metadata);

    // 批量插入向量，比逐条 insert 更高效（只需一次文件写入）。
    void insertBatch(const std::vector<VectorEntry>& entries);

    // 删除指定 ID 的向量。
    bool remove(const std::string& id);

    // 更新指定 ID 的嵌入向量（保留元数据不变）。
    void update(const std::string& id, const std::vector<float>& embedding);

    // ── 搜索 ──

    std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 10) const override;

    // ── 查询 ──

    int count() const override;
    bool contains(const std::string& id) const override;
    // 获取指定 ID 的向量条目副本（线程安全，返回副本避免悬空指针）。
    std::optional<VectorEntry> get(const std::string& id) const;

    void saveToFile() const;

private:
    std::string db_path_;
    std::vector<VectorEntry> entries_;
    bool dirty_ = false;
    bool initialized_ = false;
    mutable std::shared_mutex mutex_;  //  读写锁：search 等读操作共享锁，insert/remove/write 互斥锁

    void loadFromFile();

    static double cosineSimilarity(
        const std::vector<float>& a,
        const std::vector<float>& b);

    static double vectorNorm(const std::vector<float>& v);
};

} // namespace retrieval
