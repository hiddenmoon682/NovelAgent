#pragma once

// 向量存储抽象接口 — 解耦语义搜索与具体存储后端。
//
// 当前实现：SqliteVectorStore（SQLite + sqlite-vec vec0 虚拟表）
//
// 所有依赖向量搜索的模块均通过此接口交互，替换后端不影响上层代码。
//
// Issue 18: EmbeddingVector 类型别名 — 当前为 std::vector<float>，
// 未来可切换为 float16/int8 量化向量类型，仅需修改此处。

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace retrieval {

// 嵌入向量类型别名 — 所有模块通过此别名使用，方便未来切换精度。
using EmbeddingVector = std::vector<float>;

// 搜索结果条目。
struct SearchResult {
    std::string id;               // 向量唯一标识
    double similarity = 0.0;      // 相关度 [0, 1]（余弦相似度经 (cos+1)/2 线性映射，越高越相关）
    nlohmann::json metadata;      // 关联元数据
};

// 向量条目（内部存储格式，供批量操作使用）。
struct VectorEntry {
    std::string id;
    EmbeddingVector embedding;
    nlohmann::json metadata;
};

// 向量存储抽象接口。
class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    // ── CRUD ──

    // 插入单个向量及其元数据（id 已存在时覆盖）。
    virtual void insert(const std::string& id,
                        const EmbeddingVector& embedding,
                        const nlohmann::json& metadata) = 0;

    // 批量插入向量（同 id 条目覆盖已有值）。
    virtual void insertBatch(const std::vector<VectorEntry>& entries) = 0;

    // 删除指定 ID 的向量；存在并删除成功返回 true。
    virtual bool remove(const std::string& id) = 0;

    // 更新指定 ID 的嵌入向量（元数据保留不变）。
    virtual void update(const std::string& id,
                        const EmbeddingVector& embedding) = 0;

    // ── 搜索 ──

    // 相似度搜索：返回按 similarity 降序的前 top_k 条结果。
    virtual std::vector<SearchResult> search(
        const EmbeddingVector& query_embedding,
        int top_k = 10) const = 0;

    // ── 查询 ──

    // 返回当前存储的向量总数。
    virtual int count() const = 0;

    // 判断指定 ID 的向量是否存在。
    virtual bool contains(const std::string& id) const = 0;

    // ── 持久化 ──

    // 将未落盘变更写入持久化后端（默认空实现，供无需显式落盘的后端使用）。
    virtual void flush() {}
};

} // namespace retrieval
