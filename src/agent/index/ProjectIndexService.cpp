// ProjectIndexService 实现 — 基于内容哈希清单的增量索引。

#include "agent/index/ProjectIndexService.h"

#include "agent/memory/LongTermMemoryStore.h"
#include "project/Models/Project.h"
#include "project/ProjectIO.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "retrieval/IVectorStore.h"
#include "retrieval/NovelChunker.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <map>

namespace agent {

namespace {

// 待索引源 — 内容 + 切分出的 chunks（仅哈希变化的源才切分/嵌入）。
struct PendingSource {
    std::string content_hash;
    std::vector<retrieval::TextChunk> chunks;
};

int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

ProjectIndexService::ProjectIndexService(
    std::shared_ptr<Project> project,
    retrieval::IVectorStore& vs,
    retrieval::IEmbeddingGenerator& eg,
    LongTermMemoryStore* memory_store)
    : project_(std::move(project))
    , vector_store_(vs)
    , embedding_gen_(eg)
    , memory_store_(memory_store)
{}

std::string ProjectIndexService::manifestPath() const
{
    return project_->path + "/.novelagent/index_manifest.json";
}

IndexResult ProjectIndexService::indexAll(
    std::function<void(const std::string&)> progress,
    bool force)
{
    IndexResult result;

    if (!project_ || project_->path.empty()) {
        result.error = "未打开项目";
        return result;
    }

    auto report = [&](const std::string& msg) {
        if (progress) progress(msg);
    };

    IndexManifest manifest;
    manifest.load(manifestPath());

    // ── 模型指纹校验：模型/维度变化时整库失效 ──
    const std::string model = embedding_gen_.modelName();
    bool store_dirty = false;  // 向量库有未落盘的删除时置位（早退分支也需 flush）
    if (force || !manifest.fingerprintMatches(model, embedding_gen_.dimension())) {
        if (!manifest.sources().empty()) {
            report(force ? "强制全量重建索引..."
                         : "嵌入模型已变更 (" + manifest.embeddingModel() + " → "
                           + model + ")，整库重建...");
            for (const auto& id : manifest.allChunkIds()) {
                vector_store_.remove(id);
            }
            manifest.clear();
            store_dirty = true;
        }
    }

    // ── 收集当前项目的全部索引源，变更的才切分 ──
    retrieval::NovelChunker chunker;
    std::map<std::string, PendingSource> desired;   // source_key → 待处理源
    std::vector<std::string> unchanged_keys;        // 哈希未变的源

    // 章节：内容 = Markdown 正文
    for (const auto& ch : project_->outline.chapters) {
        if (ch.file_path.empty()) continue;
        std::string md = ProjectIO::readChapter(project_->path, ch.file_path);
        if (md.empty()) continue;
        ++result.chapters;

        const std::string key = "chapter:" + ch.id;
        const std::string hash = IndexManifest::hashContent(md);
        const ManifestEntry* prev = manifest.find(key);
        if (prev && prev->content_hash == hash) {
            unchanged_keys.push_back(key);
            continue;
        }
        PendingSource ps;
        ps.content_hash = hash;
        ps.chunks = chunker.chunkChapter(ch, md);
        desired[key] = std::move(ps);
    }

    // 单 chunk 实体源的通用处理
    auto collectEntity = [&](const std::string& key, const std::string& text,
                             retrieval::TextChunk chunk) {
        const std::string hash = IndexManifest::hashContent(text);
        const ManifestEntry* prev = manifest.find(key);
        if (prev && prev->content_hash == hash) {
            unchanged_keys.push_back(key);
            return;
        }
        PendingSource ps;
        ps.content_hash = hash;
        ps.chunks.push_back(std::move(chunk));
        desired[key] = std::move(ps);
    };

    for (const auto& c : project_->characters) {
        std::string text = retrieval::NovelChunker::chunkCharacter(c);
        if (text.empty()) continue;
        ++result.characters;
        collectEntity("char:" + c.id, text,
                      retrieval::TextChunk::characterChunk(c.id, text));
    }

    for (const auto& s : project_->settings) {
        std::string text = retrieval::NovelChunker::chunkSetting(s);
        if (text.empty()) continue;
        ++result.settings;
        collectEntity("setting:" + s.id, text,
                      retrieval::TextChunk::settingChunk(s.id, text));
    }

    for (const auto& r : project_->world_rules) {
        std::string text = retrieval::NovelChunker::chunkWorldRule(r);
        if (text.empty()) continue;
        ++result.world_rules;
        collectEntity("rule:" + r.id, text,
                      retrieval::TextChunk::worldRuleChunk(r.id, text));
    }

    // 长期记忆：日志为事实源，向量为派生索引
    if (memory_store_ && memory_store_->initialized()) {
        for (const auto& m : memory_store_->entries()) {
            if (m.text.empty()) continue;
            ++result.memories;
            retrieval::TextChunk chunk;
            chunk.id = "memory-" + m.id;
            chunk.text = m.text;
            chunk.metadata = {
                {"type", "memory"},
                {"memory_id", m.id},
                {"kind", m.kind},
                {"created_at", m.created_at},
                {"text", m.text}
            };
            collectEntity("memory:" + m.id, m.text, std::move(chunk));
        }
    }

    // ── 孤儿清理：清单中存在但项目中已删除的源 ──
    {
        std::vector<std::string> orphans;
        for (const auto& [key, entry] : manifest.sources()) {
            bool alive = desired.count(key) > 0
                || std::find(unchanged_keys.begin(), unchanged_keys.end(), key)
                   != unchanged_keys.end();
            if (!alive) orphans.push_back(key);
        }
        for (const auto& key : orphans) {
            const ManifestEntry* entry = manifest.find(key);
            if (entry) {
                for (const auto& id : entry->chunk_ids) {
                    vector_store_.remove(id);
                }
            }
            manifest.erase(key);
            ++result.removed_sources;
        }
    }

    result.skipped_sources = static_cast<int>(unchanged_keys.size());
    result.updated_sources = static_cast<int>(desired.size());

    report("索引扫描: " + std::to_string(result.updated_sources) + " 个源需更新, "
         + std::to_string(result.skipped_sources) + " 个未变化, "
         + std::to_string(result.removed_sources) + " 个已删除");

    if (desired.empty()) {
        // 无内容变化：仅孤儿清理发生时才落盘向量库，避免每次响应后全量重写 vectors.json。
        // 顺序必须先向量库后清单：清单落后于向量库只会导致下次重嵌入（安全），
        // 反之哈希跳过机制会让缺失的向量永不被补齐。
        if (result.removed_sources > 0 || store_dirty) {
            try {
                vector_store_.flush();
            } catch (const std::exception& e) {
                result.error = std::string("向量库落盘失败: ") + e.what();
                return result;
            }
        }
        manifest.setModelFingerprint(model, embedding_gen_.dimension());
        // D3: 与下方全量路径保持一致——清单写失败不影响向量正确性，下次索引会重试
        try {
            manifest.save(manifestPath());
        } catch (const std::exception& e) {
            spdlog::warn("[ProjectIndexService] 清单保存失败（下次将重试）: {}", e.what());
        }
        report("向量索引已是最新，无需重建");
        return result;
    }

    // ── 批量嵌入所有变更源的 chunks ──
    std::vector<std::string> texts;
    std::vector<std::pair<std::string, size_t>> chunk_owner;  // (source_key, chunk 下标)
    for (const auto& [key, ps] : desired) {
        for (size_t i = 0; i < ps.chunks.size(); ++i) {
            texts.push_back(ps.chunks[i].text);
            chunk_owner.emplace_back(key, i);
        }
    }
    result.total_chunks = static_cast<int>(texts.size());

    report("正在生成嵌入向量 (" + std::to_string(texts.size()) + " 条)...");

    std::vector<retrieval::EmbeddingVector> embeddings;
    try {
        embeddings = embedding_gen_.generateEmbeddings(texts);
    } catch (const std::exception& e) {
        // 嵌入失败不写清单——下次索引会重试这批源，向量库保持旧数据可用
        result.error = std::string("嵌入生成失败: ") + e.what();
        return result;
    }

    if (embeddings.size() != texts.size()) {
        result.error = "嵌入向量数量不匹配: " + std::to_string(embeddings.size())
                     + " vs " + std::to_string(texts.size());
        return result;
    }

    // ── 写入向量库：先删旧 chunk，再插新 chunk，最后更新清单 ──
    const int64_t now = nowEpochSeconds();
    for (auto& [key, ps] : desired) {
        if (const ManifestEntry* prev = manifest.find(key)) {
            for (const auto& id : prev->chunk_ids) {
                vector_store_.remove(id);   // 旧 chunk 数可能多于新 chunk 数
            }
        }
        ManifestEntry entry;
        entry.content_hash = ps.content_hash;
        entry.updated_at = now;
        for (const auto& c : ps.chunks) {
            entry.chunk_ids.push_back(c.id);
        }
        manifest.upsert(key, std::move(entry));
    }
    for (size_t i = 0; i < chunk_owner.size(); ++i) {
        const auto& [key, ci] = chunk_owner[i];
        const auto& chunk = desired[key].chunks[ci];
        vector_store_.insert(chunk.id, embeddings[i], chunk.metadata);
    }

    // 先落盘向量库，成功后再写清单——反序时若 flush 失败/中途崩溃，
    // 清单已声明“已按新哈希索引”，哈希跳过机制会让缺失的向量永不被补齐
    try {
        vector_store_.flush();
    } catch (const std::exception& e) {
        result.error = std::string("向量库落盘失败: ") + e.what();
        return result;
    }
    manifest.setModelFingerprint(model, embedding_gen_.dimension());
    try {
        manifest.save(manifestPath());
    } catch (const std::exception& e) {
        // 清单写失败不影响向量正确性：下次索引会重嵌入本批源
        spdlog::warn("[ProjectIndexService] 清单保存失败（下次将重试）: {}", e.what());
    }

    report("向量索引已更新: " + std::to_string(result.total_chunks) + " 个片段 ("
         + std::to_string(result.updated_sources) + " 个源) → "
         + project_->path + "/.novelagent/vectors.json");
    return result;
}

} // namespace agent
