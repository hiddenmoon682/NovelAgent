#pragma once

/// 向量存储与语义搜索模块。
///
/// 职责：存储文本嵌入向量，支持余弦相似度 ANN 搜索。
///
/// 当前实现：JSON 文件后端（暴力搜索），适用于万级向量。
/// Phase 4 后续可替换为 sqlite-vec（只需修改 .cpp 内部实现，接口不变）。
///
/// 使用流程：
///   1. VectorStore store;
///   2. store.init("path/to/vectors.json");
///   3. store.insert("ch-001-seg-0", embedding, {{"type", "chapter"}});
///   4. auto results = store.search(query_embedding, 5);

#include "retrieval/IVectorStore.h"

#include <string>
#include <vector>

namespace retrieval {

/// 向量存储 — JSON 文件后端 + 暴力余弦相似度搜索。
///
/// 实现 IVectorStore 抽象接口，API 兼容 sqlite-vec（后续替换只需修改 .cpp）。
///
/// 线程安全：不安全。同一实例不应并发调用。
class VectorStore : public IVectorStore {
public:
    VectorStore() = default;
    ~VectorStore() = default;

    // ── 生命周期 ──

    /// 初始化存储，打开或创建向量文件。
    /// @param db_path 向量文件的完整路径（如 .novelagent/vectors.json）
    void init(const std::string& db_path);

    /// 关闭存储，自动保存未写入的变更。
    void close();

    // ── CRUD ──

    /// 插入单个向量及其元数据。
    /// 若 id 已存在则覆盖。
    /// @param id         向量唯一标识
    /// @param embedding  浮点嵌入向量（维度由 EmbeddingGenerator 决定）
    /// @param metadata   关联元数据（type, chapter_id, chunk_index 等）
    void insert(const std::string& id,
                const std::vector<float>& embedding,
                const nlohmann::json& metadata);

    /// 批量插入向量，比逐条 insert 更高效（只需一次文件写入）。
    /// @param entries 向量条目列表
    void insertBatch(const std::vector<VectorEntry>& entries);

    /// 删除指定 ID 的向量。
    /// @param id 向量 ID
    /// @return    是否找到并删除了条目
    bool remove(const std::string& id);

    /// 更新指定 ID 的嵌入向量（保留元数据不变）。
    /// 若 id 不存在则等同于 insert。
    /// @param id         向量 ID
    /// @param embedding  新的嵌入向量
    void update(const std::string& id, const std::vector<float>& embedding);

    // ── 搜索 ──

    /// ANN 语义搜索 — 余弦相似度 Top-K。
    ///
    /// 当前实现：暴力遍历所有向量计算余弦相似度。
    /// 对于万级向量规模，延迟 < 10ms。
    ///
    /// @param query_embedding  查询向量
    /// @param top_k            返回最相似的前 K 条
    /// @return                 按相似度降序排列的搜索结果
    std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 10) const;

    // ── 查询 ──

    /// 返回存储中的向量总数。
    int count() const;

    /// 检查指定 ID 的向量是否存在。
    bool contains(const std::string& id) const;

    /// 获取指定 ID 的向量条目（用于调试）。
    /// @return 条目指针；不存在时返回 nullptr（调用方不得持有此指针超过下一次修改操作）
    const VectorEntry* get(const std::string& id) const;

private:
    std::string db_path_;
    std::vector<VectorEntry> entries_;     // 内存中的向量列表
    bool dirty_ = false;                   // 标记是否有未保存的变更
    bool initialized_ = false;

    /// 从 JSON 文件加载所有向量到内存。
    void loadFromFile();

    /// 将内存中的向量保存到 JSON 文件。
    void saveToFile() const;

    /// 计算两个向量的余弦相似度。
    /// @return [0, 1] 区间内的相似度值
    static double cosineSimilarity(
        const std::vector<float>& a,
        const std::vector<float>& b);

    /// 计算向量的 L2 范数。
    static double vectorNorm(const std::vector<float>& v);
};

} // namespace retrieval
