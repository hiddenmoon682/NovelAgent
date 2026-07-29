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
    //
    // 文件存在时立即全量加载到内存；不存在或格式无效时从空库开始
    // （不报错，首次 saveToFile/close 时才创建文件）。可重复调用（重新加载）。
    //
    // @param db_path 向量文件的完整路径（如 .novelagent/vectors.json），
    //                后续持久化均写回此路径，父目录不存在时保存前自动创建。
    void init(const std::string& db_path);

    // 关闭存储：自动保存未写入的变更（dirty 时），并清空内存中的向量。
    void close();

    // ── CRUD ──

    // 插入单个向量及其元数据（id 已存在时覆盖）。
    //
    // @param id        向量唯一标识（如 "ch-001-seg-0"）。
    // @param embedding 嵌入向量，按值拷贝存入。
    // @param metadata  关联元数据（如 {"type": "chapter"}）。
    void insert(const std::string& id,
                const std::vector<float>& embedding,
                const nlohmann::json& metadata);

    // 批量插入向量，比逐条 insert 更高效（只需一次锁获取）。
    //
    // @param entries 待插入的向量条目列表，同 id 条目覆盖已有值。
    void insertBatch(const std::vector<VectorEntry>& entries);

    // 删除指定 ID 的向量。
    //
    // @param id 待删除的向量 ID。
    // @return 存在并删除成功返回 true，不存在返回 false。
    bool remove(const std::string& id);

    // 更新指定 ID 的嵌入向量（保留元数据不变；id 不存在时以空元数据新建条目）。
    //
    // @param id        目标向量 ID。
    // @param embedding 新的嵌入向量。
    void update(const std::string& id, const std::vector<float>& embedding);

    // ── 搜索 ──

    // 余弦相似度搜索（全量扫描）。
    //
    // @param query_embedding 查询向量，维度应与库内向量一致
    //                        （维度不匹配的条目相似度记为 0，不报错）。
    // @param top_k           返回条数上限；<= 0 时返回空列表。
    // @return 按 similarity 降序排列的前 top_k 条结果（不足 top_k 时全部返回）；
    //         similarity 为映射到 [0, 1] 的余弦相似度，越高越相关。
    std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 10) const override;

    // ── 查询 ──

    // 返回当前存储的向量总数。
    int count() const override;

    // 判断指定 ID 的向量是否存在。
    bool contains(const std::string& id) const override;

    // 获取指定 ID 的向量条目。
    //
    // @param id 目标向量 ID。
    // @return 命中时返回条目副本（而非指针/引用，避免锁释放后悬空），
    //         未命中返回 std::nullopt。
    std::optional<VectorEntry> get(const std::string& id) const;

    // 将全部向量写回 db_path 文件（父目录不存在时自动创建）。
    void saveToFile() const;

    // 持久化接口实现：等价于 saveToFile()。
    void flush() override { saveToFile(); }

private:
    std::string db_path_;
    std::vector<VectorEntry> entries_;
    bool dirty_ = false;
    bool initialized_ = false;
    // WHY 选 shared_mutex 而非普通 mutex：search() 是每轮对话 RAG 检索都会
    // 走到的高频读路径，且暴力扫描耗时随向量数增长，若用互斥锁会让并发
    // 读（如前台检索 vs 后台索引服务查询）串行化；读操作用 shared_lock 可
    // 并行。写操作（insert/remove 等）用 unique_lock 互斥：vector 扩容/删除
    // 会重分配内存，与读并发会产生撕裂读（悬空迭代器/半写入条目）。
    mutable std::shared_mutex mutex_;

    void loadFromFile();

    static double cosineSimilarity(
        const std::vector<float>& a,
        const std::vector<float>& b);

    static double vectorNorm(const std::vector<float>& v);
};

} // namespace retrieval
