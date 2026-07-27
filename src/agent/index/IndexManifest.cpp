// IndexManifest 实现 — 索引清单的加载/保存与内容哈希。

#include "agent/index/IndexManifest.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <cinttypes>
#include <cstdio>

using json = nlohmann::json;

namespace agent {

// ===========================================================================
// 持久化
// ===========================================================================

void IndexManifest::load(const std::string& path)
{
    sources_.clear();
    embedding_model_.clear();
    dimension_ = 0;

    if (!utils::file::exists(path)) {
        spdlog::debug("[IndexManifest] 清单文件不存在，从空开始: {}", path);
        return;
    }

    auto j = ProjectIO::loadJsonFile(path);
    if (!j || !j->is_object()) {
        spdlog::warn("[IndexManifest] 清单文件格式无效，从空开始: {}", path);
        return;
    }

    embedding_model_ = utils::json::getOrDefault(*j, "embedding_model", std::string{});
    dimension_ = utils::json::getOrDefault(*j, "dimension", 0);

    if (j->contains("sources") && (*j)["sources"].is_object()) {
        for (const auto& [key, val] : (*j)["sources"].items()) {
            if (!val.is_object()) continue;
            ManifestEntry entry;
            entry.content_hash = utils::json::getOrDefault(val, "content_hash", std::string{});
            entry.updated_at = utils::json::getOrDefault(val, "updated_at", int64_t{0});
            if (val.contains("chunk_ids") && val["chunk_ids"].is_array()) {
                for (const auto& id : val["chunk_ids"]) {
                    if (id.is_string()) entry.chunk_ids.push_back(id.get<std::string>());
                }
            }
            sources_[key] = std::move(entry);
        }
    }

    spdlog::debug("[IndexManifest] 已加载清单: {} 个源, 模型={}",
                  sources_.size(), embedding_model_);
}

void IndexManifest::save(const std::string& path) const
{
    json j;
    j["embedding_model"] = embedding_model_;
    j["dimension"] = dimension_;

    json srcs = json::object();
    for (const auto& [key, entry] : sources_) {
        srcs[key] = {
            {"content_hash", entry.content_hash},
            {"chunk_ids", entry.chunk_ids},
            {"updated_at", entry.updated_at}
        };
    }
    j["sources"] = std::move(srcs);

    const std::string dir = utils::file::dirName(path);
    if (!dir.empty() && !utils::file::exists(dir)) {
        utils::file::createDirs(dir);
    }
    ProjectIO::saveJsonFile(path, j);
    spdlog::debug("[IndexManifest] 已保存清单: {} 个源", sources_.size());
}

// ===========================================================================
// 模型指纹
// ===========================================================================

void IndexManifest::setModelFingerprint(std::string model, int dimension)
{
    embedding_model_ = std::move(model);
    dimension_ = dimension;
}

bool IndexManifest::fingerprintMatches(const std::string& model, int dimension) const
{
    if (embedding_model_.empty()) return sources_.empty();  // 空清单视为兼容
    if (embedding_model_ != model) return false;
    // 维度 0 表示尚未确定（首次嵌入前），不参与比较
    if (dimension_ != 0 && dimension != 0 && dimension_ != dimension) return false;
    return true;
}

// ===========================================================================
// 条目操作
// ===========================================================================

bool IndexManifest::contains(const std::string& source_key) const
{
    return sources_.count(source_key) > 0;
}

const ManifestEntry* IndexManifest::find(const std::string& source_key) const
{
    auto it = sources_.find(source_key);
    return it != sources_.end() ? &it->second : nullptr;
}

void IndexManifest::upsert(const std::string& source_key, ManifestEntry entry)
{
    sources_[source_key] = std::move(entry);
}

bool IndexManifest::erase(const std::string& source_key)
{
    return sources_.erase(source_key) > 0;
}

void IndexManifest::clear()
{
    sources_.clear();
}

std::vector<std::string> IndexManifest::allChunkIds() const
{
    std::vector<std::string> ids;
    for (const auto& [key, entry] : sources_) {
        ids.insert(ids.end(), entry.chunk_ids.begin(), entry.chunk_ids.end());
    }
    return ids;
}

// ===========================================================================
// 内容哈希（FNV-1a 64 位）
// ===========================================================================

std::string IndexManifest::hashContent(const std::string& content)
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016" PRIx64, hash);
    return std::string(buf);
}

} // namespace agent
