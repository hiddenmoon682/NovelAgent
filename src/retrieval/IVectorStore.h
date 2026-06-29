#pragma once

/// 向量存储抽象接口 — 解耦语义搜索与具体存储后端。
///
/// 当前实现：JsonVectorStore（JSON 文件 + 暴力余弦相似度）
/// 未来可选：SqliteVectorStore（sqlite-vec ANN 搜索）
///
/// 所有依赖向量搜索的模块均通过此接口交互，替换后端不影响上层代码。

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace retrieval {

/// 搜索结果条目。
struct SearchResult {
    std::string id;               // 向量唯一标识
    double similarity = 0.0;      // 相关度 [0, 1]（余弦相似度经 (cos+1)/2 线性映射，越高越相关）
    nlohmann::json metadata;      // 关联元数据
};

/// 向量条目（内部存储格式，供批量操作使用）。
struct VectorEntry {
    std::string id;
    std::vector<float> embedding;
    nlohmann::json metadata;
};

/// 向量存储抽象接口。
class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    // ── CRUD ──

    virtual void insert(const std::string& id,
                        const std::vector<float>& embedding,
                        const nlohmann::json& metadata) = 0;

    virtual void insertBatch(const std::vector<VectorEntry>& entries) = 0;

    virtual bool remove(const std::string& id) = 0;

    virtual void update(const std::string& id,
                        const std::vector<float>& embedding) = 0;

    // ── 搜索 ──

    virtual std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 10) const = 0;

    // ── 查询 ──

    virtual int count() const = 0;

    virtual bool contains(const std::string& id) const = 0;
};

} // namespace retrieval
