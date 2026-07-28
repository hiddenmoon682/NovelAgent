#pragma once

// IndexManifest — 向量索引清单，保证 RAG 数据时效性的核心组件。
//
// 记录每个索引源（章节/角色/设定/世界规则/长期记忆）的内容哈希与
// 对应 chunk id 列表，实现三重可靠性保证：
//   1. 增量索引：内容哈希未变化的源跳过重嵌入（省时省钱）
//   2. 孤儿清理：源被删除后，其遗留向量随下次索引自动移除
//   3. 模型指纹：嵌入模型或维度变化时整库失效重建，
//      避免不同维度向量混存导致检索静默失败
//
// 持久化位置：<project>/.novelagent/index_manifest.json

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace agent {

// 单个索引源的清单条目。
struct ManifestEntry {
    std::string content_hash;             // 源内容的 FNV-1a 哈希（十六进制）
    std::vector<std::string> chunk_ids;   // 该源在向量库中的全部 chunk id
    int64_t updated_at = 0;               // 最近一次索引时间（epoch 秒）
};

// 索引清单 — 源标识（如 "chapter:ch-001"）到清单条目的映射。
class IndexManifest {
public:
    // 从文件加载清单；文件不存在或损坏时从空开始。
    void load(const std::string& path);

    // 保存清单到文件（覆盖写入）。
    void save(const std::string& path) const;

    // ── 模型指纹 ──

    const std::string& embeddingModel() const { return embedding_model_; }
    int dimension() const { return dimension_; }
    void setModelFingerprint(std::string model, int dimension);

    // 判断当前指纹与给定模型是否兼容（模型名一致；维度 0 表示未知，不比较）。
    bool fingerprintMatches(const std::string& model, int dimension) const;

    // ── 条目操作 ──

    const std::map<std::string, ManifestEntry>& sources() const { return sources_; }
    bool contains(const std::string& source_key) const;
    // 返回指定源的条目指针；不存在返回 nullptr。
    const ManifestEntry* find(const std::string& source_key) const;
    void upsert(const std::string& source_key, ManifestEntry entry);
    bool erase(const std::string& source_key);
    void clear();

    // 全部已跟踪的 chunk id（用于全量失效时清理向量库）。
    std::vector<std::string> allChunkIds() const;

    // ── 内容哈希 ──

    // FNV-1a 64 位哈希，返回十六进制字符串。
    static std::string hashContent(const std::string& content);

private:
    std::string embedding_model_;
    int dimension_ = 0;
    std::map<std::string, ManifestEntry> sources_;
};

} // namespace agent
