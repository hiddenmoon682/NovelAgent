#pragma once

/// 向量存储抽象接口 — 解耦语义搜索与具体存储后端。
///
/// 当前实现：JsonVectorStore（JSON 文件 + 暴力余弦相似度）
/// 未来可选：SqliteVectorStore（sqlite-vec ANN 搜索）
///
/// 所有依赖向量搜索的模块均通过此接口交互，替换后端不影响上层代码。
///
/// Issue 18: EmbeddingVector 类型别名 — 当前为 std::vector<float>，
/// 未来可切换为 float16/int8 量化向量类型，仅需修改此处。

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace retrieval {

/// 嵌入向量类型别名 — 所有模块通过此别名使用，方便未来切换精度。
using EmbeddingVector = std::vector<float>;

/// 搜索结果条目。
struct SearchResult {
    std::string id;               // 向量唯一标识
    double similarity = 0.0;      // 相关度 [0, 1]（余弦相似度经 (cos+1)/2 线性映射，越高越相关）
    nlohmann::json metadata;      // 关联元数据
};

/// 向量条目（内部存储格式，供批量操作使用）。
struct VectorEntry {
    std::string id;
    EmbeddingVector embedding;
    nlohmann::json metadata;
};

/// 向量存储抽象接口。
class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    // ── CRUD ──

    virtual void insert(const std::string& id,
                        const EmbeddingVector& embedding,
                        const nlohmann::json& metadata) = 0;

    virtual void insertBatch(const std::vector<VectorEntry>& entries) = 0;

    virtual bool remove(const std::string& id) = 0;

    virtual void update(const std::string& id,
                        const EmbeddingVector& embedding) = 0;

    // ── 搜索 ──

    virtual std::vector<SearchResult> search(
        const EmbeddingVector& query_embedding,
        int top_k = 10) const = 0;

    // ── 查询 ──

    virtual int count() const = 0;

    virtual bool contains(const std::string& id) const = 0;
};

} // namespace retrieval
