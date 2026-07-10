// VectorStore 实现 — JSON 文件后端 + 暴力余弦相似度搜索。
//
// 向量文件格式（JSON 数组）:
// [
//   {
//     "id": "ch-001-seg-0",
//     "embedding": [0.123, -0.456, ...],
//     "metadata": {"type": "chapter", "chapter_id": "ch-001", "chunk_index": 0}
//   },
//   ...
// ]

#include "retrieval/VectorStore.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>

using json = nlohmann::json;

namespace retrieval {

// ===========================================================================
// 生命周期
// ===========================================================================

void VectorStore::init(const std::string& db_path)
{
    std::unique_lock lock(mutex_);
    db_path_ = db_path;
    loadFromFile();
    initialized_ = true;
    spdlog::info("[VectorStore] 已初始化: {} ({} 条向量)", db_path_, entries_.size());
}

void VectorStore::close()
{
    std::unique_lock lock(mutex_);
    if (dirty_) {
        saveToFile();
    }
    entries_.clear();
    initialized_ = false;
    spdlog::debug("[VectorStore] 已关闭");
}

// ===========================================================================
// CRUD
// ===========================================================================

void VectorStore::insert(
    const std::string& id,
    const std::vector<float>& embedding,
    const json& metadata)
{
    std::unique_lock lock(mutex_);
    // 检查是否已存在（覆盖更新）
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&id](const VectorEntry& e) { return e.id == id; });

    if (it != entries_.end()) {
        it->embedding = embedding;
        it->metadata = metadata;
    } else {
        entries_.push_back({id, embedding, metadata});
    }

    dirty_ = true;
}

void VectorStore::insertBatch(const std::vector<VectorEntry>& entries)
{
    std::unique_lock lock(mutex_);
    // 逐条处理以支持覆盖
    for (const auto& entry : entries) {
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [&entry](const VectorEntry& e) { return e.id == entry.id; });

        if (it != entries_.end()) {
            *it = entry;
        } else {
            entries_.push_back(entry);
        }
    }

    dirty_ = true;
    spdlog::debug("[VectorStore] 批量插入 {} 条向量 (总数: {})",
                  entries.size(), entries_.size());
}

bool VectorStore::remove(const std::string& id)
{
    std::unique_lock lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&id](const VectorEntry& e) { return e.id == id; });

    if (it != entries_.end()) {
        entries_.erase(it);
        dirty_ = true;
        return true;
    }

    return false;
}

void VectorStore::update(const std::string& id, const std::vector<float>& embedding)
{
    std::unique_lock lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&id](const VectorEntry& e) { return e.id == id; });

    if (it != entries_.end()) {
        it->embedding = embedding;
    } else {
        entries_.push_back({id, embedding, json::object()});
    }

    dirty_ = true;
}

// ===========================================================================
// 搜索
// ===========================================================================

std::vector<SearchResult> VectorStore::search(
    const std::vector<float>& query_embedding,
    int top_k) const
{
    std::shared_lock lock(mutex_);
    if (entries_.empty() || top_k <= 0) {
        return {};
    }

    // 计算所有向量的余弦相似度
    std::vector<SearchResult> results;
    results.reserve(entries_.size());

    for (const auto& entry : entries_) {
        if (entry.embedding.empty()) continue;

        double sim = cosineSimilarity(query_embedding, entry.embedding);
        results.push_back({entry.id, sim, entry.metadata});
    }

    // 按相似度降序排序
    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) {
            return a.similarity > b.similarity;
        });

    // 截取 Top-K
    if (static_cast<int>(results.size()) > top_k) {
        results.resize(top_k);
    }

    return results;
}

// ===========================================================================
// 查询
// ===========================================================================

int VectorStore::count() const
{
    std::shared_lock lock(mutex_);
    return static_cast<int>(entries_.size());
}

bool VectorStore::contains(const std::string& id) const
{
    std::shared_lock lock(mutex_);
    return std::any_of(entries_.begin(), entries_.end(),
        [&id](const VectorEntry& e) { return e.id == id; });
}

std::optional<VectorEntry> VectorStore::get(const std::string& id) const
{
    std::shared_lock lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&id](const VectorEntry& e) { return e.id == id; });

    if (it != entries_.end()) {
        return *it;  // 返回副本，避免锁释放后的悬空指针
    }
    return std::nullopt;
}

// ===========================================================================
// 文件 I/O
// ===========================================================================

void VectorStore::loadFromFile()
{
    entries_.clear();

    if (!utils::file::exists(db_path_)) {
        spdlog::debug("[VectorStore] 向量文件不存在，从空开始: {}", db_path_);
        return;
    }

    auto j = ProjectIO::loadJsonFile(db_path_);
    if (!j || !j->is_array()) {
        spdlog::warn("[VectorStore] 向量文件格式无效，从空开始: {}", db_path_);
        return;
    }

    for (const auto& item : *j) {
        VectorEntry entry;
        entry.id = utils::json::getOrDefault(item, "id", std::string{});
        entry.metadata = utils::json::getOrDefault(item, "metadata", json::object());

        if (item.contains("embedding") && item["embedding"].is_array()) {
            for (const auto& val : item["embedding"]) {
                if (val.is_number()) {
                    entry.embedding.push_back(val.get<float>());
                }
            }
        }

        if (!entry.id.empty() && !entry.embedding.empty()) {
            entries_.push_back(std::move(entry));
        }
    }

    spdlog::debug("[VectorStore] 从文件加载 {} 条向量", entries_.size());
}

void VectorStore::saveToFile() const
{
    json j = json::array();

    for (const auto& entry : entries_) {
        json item;
        item["id"] = entry.id;
        item["embedding"] = entry.embedding;
        item["metadata"] = entry.metadata;
        j.push_back(item);
    }

    // 确保父目录存在
    const std::string dir = utils::file::dirName(db_path_);
    if (!dir.empty() && !utils::file::exists(dir)) {
        utils::file::createDirs(dir);
    }

    ProjectIO::saveJsonFile(db_path_, j);
    spdlog::debug("[VectorStore] 保存 {} 条向量到文件", entries_.size());
}

// ===========================================================================
// 余弦相似度
// ===========================================================================

double VectorStore::cosineSimilarity(
    const std::vector<float>& a,
    const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }

    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;

    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        norm_a += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        norm_b += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }

    double denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom < 1e-10) {
        return 0.0;
    }

    // 余弦相似度范围 [-1, 1]，映射到 [0, 1]
    double cos_sim = dot / denom;
    return (cos_sim + 1.0) / 2.0;
}

double VectorStore::vectorNorm(const std::vector<float>& v)
{
    double sum = 0.0;
    for (float val : v) {
        sum += static_cast<double>(val) * static_cast<double>(val);
    }
    return std::sqrt(sum);
}

} // namespace retrieval
