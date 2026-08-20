// ProjectIndexService 实现 — 基于内容哈希清单的增量索引（SQLite 单事务）。

#include "agent/index/ProjectIndexService.h"

#include "agent/longterm/LongTermMemoryStore.h"
#include "project/Models/Project.h"
#include "project/ProjectAccess.h"
#include "project/ProjectIO.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "retrieval/NovelChunker.h"
#include "storage/SqliteStore.h"

#include <SQLiteCpp/Statement.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;

namespace agent {

namespace {

// 待索引源 — 内容 + 切分出的 chunks（仅哈希变化的源才切分/嵌入）。
struct PendingSource {
    std::string content_hash;
    std::vector<retrieval::TextChunk> chunks;
};

// 清单历史条目（SQL 快照，供哈希比对与孤儿清理）。
struct PrevEntry {
    std::string content_hash;
    std::vector<std::string> chunk_ids;
};

int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// FNV-1a 64 位内容哈希（与旧 IndexManifest::hashContent 算法一致）。
std::string hashContent(const std::string& content)
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

// 解析 kv_store 中存储的整数；空串/非法返回 0。
int parseInt(const std::string& s)
{
    if (s.empty()) return 0;
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

// 块头注入格式版本：调整 buildChapterHeader 输出格式（影响嵌入文本）时必须升版本，
// 否则正文 hash 未变的库会被增量索引跳过，新格式永不生效。
constexpr const char* kContextVersion = "v1";

} // namespace

ProjectIndexService::ProjectIndexService(
    std::shared_ptr<ProjectAccess> access,
    storage::SqliteStore& sqlite,
    retrieval::IEmbeddingGenerator& eg,
    LongTermMemoryStore* memory_store)
    : project_access_(std::move(access))
    , sqlite_(sqlite)
    , embedding_gen_(eg)
    , memory_store_(memory_store)
{}

namespace {

// 读取全部清单条目（source_key → {hash, chunk_ids}）（锁内调用）。
std::map<std::string, PrevEntry> loadAllPrevEntries(SQLite::Database& db)
{
    std::map<std::string, PrevEntry> out;
    SQLite::Statement stmt(db,
        "SELECT src.source_key, src.content_hash, ch.chunk_id "
        "FROM index_sources src "
        "LEFT JOIN index_chunks ch ON ch.source_key = src.source_key "
        "ORDER BY src.source_key");
    while (stmt.executeStep()) {
        const std::string key = stmt.getColumn(0).getString();
        if (!stmt.getColumn(1).isNull()) {
            out[key].content_hash = stmt.getColumn(1).getString();
        }
        if (!stmt.getColumn(2).isNull()) {
            out[key].chunk_ids.push_back(stmt.getColumn(2).getString());
        }
    }
    return out;
}

// 删除指定源的向量与清单（锁内调用）。
void deleteSourceVectorsAndManifest(SQLite::Database& db, const std::string& key,
                                    const PrevEntry& prev)
{
    for (const auto& id : prev.chunk_ids) {
        SQLite::Statement del(db, "DELETE FROM vec_chunks WHERE chunk_id = ?");
        del.bind(1, id);
        del.exec();
    }
    SQLite::Statement del(db, "DELETE FROM index_sources WHERE source_key = ?");
    del.bind(1, key);
    del.exec();
}

// 写模型指纹（dim==0 且模型没变时保留已知维度，防清零静默失效）。
void writeFingerprint(storage::SqliteStore& s, const std::string& model, int dim)
{
    int final_dim = dim;
    if (dim == 0 && model == s.getKV("embedding_model")) {
        final_dim = parseInt(s.getKV("embedding_dimension"));
    }
    s.setKV("embedding_model", model);
    s.setKV("embedding_dimension", std::to_string(final_dim));
    s.setKV("context_version", kContextVersion);
}

} // namespace

IndexResult ProjectIndexService::indexAll(
    std::function<void(const std::string&)> progress,
    bool force)
{
    std::lock_guard<std::mutex> lock(index_mutex_);
    IndexResult result;

    if (!project_access_ || project_access_->path().empty()) {
        result.error = "未打开项目";
        return result;
    }
    if (!sqlite_.isOpen()) {
        result.error = "SQLite 库未打开";
        return result;
    }

    auto report = [&](const std::string& msg) {
        if (progress) progress(msg);
    };

    // 阶段检查点 1：进入索引流程即上报。进度回调可在此抛取消异常，
    // 使 indexAll 在任何 SQLite/成员访问之前安全中止（应用析构时的退场通道）。
    report("开始索引...");

    const std::string model = embedding_gen_.modelName();
    const int dim = embedding_gen_.dimension();

    // ── 模型指纹校验：模型/维度变化或强制时整库失效（单事务）──
    // 注：不可在 withLock 回调内再调 inTransaction（同锁不可重入），
    // 校验与失效合并进同一个 inTransaction。
    bool wiped = false;
    std::string wipe_reason;
    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        const std::string saved_model = s.getKV("embedding_model");
        const int saved_dim = parseInt(s.getKV("embedding_dimension"));
        const std::string saved_ctx_version = s.getKV("context_version");
        std::map<std::string, PrevEntry> prevs = loadAllPrevEntries(s.db());
        const bool has_sources = !prevs.empty();

        const bool incompatible =
            !saved_model.empty() && saved_model != model;
        const bool dim_changed =
            saved_dim != 0 && dim != 0 && saved_dim != dim;
        const bool orphan_fingerprint =
            saved_model.empty() && has_sources;  // 有源但无指纹（异常态）

        // 块头格式版本不符（含旧库无 key 的空串）且库中已有源 → 整库重建；
        // 全新空库不触发（has_sources 限定），避免首启误报重建
        const bool ctx_version_changed =
            has_sources && saved_ctx_version != kContextVersion;

        if (force && has_sources) {
            wipe_reason = "强制全量重建索引...";
        } else if (incompatible || dim_changed || orphan_fingerprint) {
            wipe_reason = "嵌入模型已变更 (" + saved_model + " → "
                        + model + ")，整库重建...";
        } else if (ctx_version_changed) {
            wipe_reason = "上下文增强版本已变更 (" + (saved_ctx_version.empty() ? "旧版" : saved_ctx_version)
                        + " → " + kContextVersion + ")，整库重建...";
        }

        if (!wipe_reason.empty()) {
            s.resetVectorTable();   // DROP vec_chunks + 清维度缓存（写入时重建）
            s.db().exec("DELETE FROM index_sources");  // 级联清空 index_chunks
            s.setKV("embedding_model", "");
            s.setKV("embedding_dimension", "");
            s.setKV("context_version", "");  // 与模型指纹对称清空，防非 wipe 路径遗留旧版本号
            wiped = true;
        }
    });
    if (wiped) report(wipe_reason);

    // ── 收集当前项目的全部索引源并读取前次清单快照 ──
    retrieval::NovelChunker chunker;
    std::map<std::string, PendingSource> desired;
    std::vector<std::string> unchanged_keys;

    std::map<std::string, PrevEntry> prevs;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        if (wiped) {
            prevs.clear();  // 整库失效后清单表已清空，跳过第二次全表扫描
        } else {
            prevs = loadAllPrevEntries(s.db());
        }
    });

    std::vector<Chapter> chapters;
    std::vector<Character> chars;
    std::vector<Setting> settings;
    std::vector<WorldRule> world_rules;
    const std::string project_path = project_access_->path();
    project_access_->withReadLock([&](const Project& p) {
        chapters = p.outline.chapters;
        chars = p.characters;
        settings = p.settings;
        world_rules = p.world_rules;
    });

    // 章节块头注入用 id→名字 字典：Chapter 存的是实体 id，需解析为名字才有语义。
    // 查不到名字（词典缺失）的 id 在 buildChapterHeader 中被跳过。
    retrieval::ChapterContext chapter_ctx;
    for (const auto& c : chars) {
        if (!c.name.empty()) chapter_ctx.character_names[c.id] = c.name;
    }
    for (const auto& s : settings) {
        if (!s.name.empty()) chapter_ctx.setting_names[s.id] = s.name;
    }

    for (const auto& ch : chapters) {
        if (ch.file_path.empty()) continue;
        std::string chapter_text = ProjectIO::readChapter(project_path, ch.file_path);
        if (chapter_text.empty()) continue;
        ++result.chapters;

        const std::string key = "chapter:" + ch.id;
        const std::string hash = hashContent(chapter_text);
        const auto it = prevs.find(key);
        if (it != prevs.end() && it->second.content_hash == hash) {
            unchanged_keys.push_back(key);
            continue;
        }
        PendingSource ps;
        ps.content_hash = hash;
        ps.chunks = chunker.chunkChapter(ch, chapter_text, chapter_ctx);
        desired[key] = std::move(ps);
    }

    auto collectEntity = [&](const std::string& key, const std::string& text,
                             retrieval::TextChunk chunk) {
        const std::string hash = hashContent(text);
        const auto it = prevs.find(key);
        if (it != prevs.end() && it->second.content_hash == hash) {
            unchanged_keys.push_back(key);
            return;
        }
        PendingSource ps;
        ps.content_hash = hash;
        ps.chunks.push_back(std::move(chunk));
        desired[key] = std::move(ps);
    };

    for (const auto& c : chars) {
        std::string text = retrieval::NovelChunker::chunkCharacter(c);
        if (text.empty()) continue;
        ++result.characters;
        collectEntity("char:" + c.id, text,
                      retrieval::TextChunk::characterChunk(c.id, text));
    }
    for (const auto& s : settings) {
        std::string text = retrieval::NovelChunker::chunkSetting(s);
        if (text.empty()) continue;
        ++result.settings;
        collectEntity("setting:" + s.id, text,
                      retrieval::TextChunk::settingChunk(s.id, text));
    }
    for (const auto& r : world_rules) {
        std::string text = retrieval::NovelChunker::chunkWorldRule(r);
        if (text.empty()) continue;
        ++result.world_rules;
        collectEntity("rule:" + r.id, text,
                      retrieval::TextChunk::worldRuleChunk(r.id, text));
    }

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

    // ── 孤儿清理：清单中存在但项目中已删除的源（单事务）──
    {
        int removed = 0;
        sqlite_.inTransaction([&](storage::SqliteStore& s) {
            SQLite::Database& db = s.db();
            // 拷贝 unchanged_keys 进哈希集合：孤儿判定从"全量 × 线性 find"
            // 的 O(N²) 降为 O(N)，避免大项目清点删除时逐源线性扫描。
            const std::unordered_set<std::string> unchanged_set(
                unchanged_keys.begin(), unchanged_keys.end());
            for (const auto& [key, prev] : prevs) {
                const bool alive = desired.count(key) > 0
                    || unchanged_set.count(key) > 0;
                if (!alive) {
                    deleteSourceVectorsAndManifest(db, key, prev);
                    ++removed;
                }
            }
        });
        result.removed_sources = removed;
    }

    result.skipped_sources = static_cast<int>(unchanged_keys.size());
    result.updated_sources = static_cast<int>(desired.size());

    report("索引扫描: " + std::to_string(result.updated_sources) + " 个源需更新, "
         + std::to_string(result.skipped_sources) + " 个未变化, "
         + std::to_string(result.removed_sources) + " 个已删除");

    if (desired.empty()) {
        // 无内容变化：若刚执行过整库失效，补写指纹（与旧实现的 setModelFingerprint
        // 时机一致），确保下次运行不会因空指纹反复重建。
        if (wiped) {
            sqlite_.withLock([&](storage::SqliteStore& s) {
                writeFingerprint(s, model, dim);
            });
        }
        report("向量索引已是最新，无需重建");
        return result;
    }

    // ── 批量嵌入所有变更源的 chunks ──
    std::vector<std::string> texts;
    std::vector<std::pair<std::string, size_t>> chunk_owner;
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
        result.error = std::string("嵌入生成失败: ") + e.what();
        return result;
    }

    if (embeddings.size() != texts.size()) {
        result.error = "嵌入向量数量不匹配: " + std::to_string(embeddings.size())
                     + " vs " + std::to_string(texts.size());
        return result;
    }

    // 阶段检查点 2：嵌入成功后、触碰 sqlite_ 前的最后取消机会；
    // 进度回调在此抛异常则本轮索引中止，不进入最终事务。
    report("正在写入向量索引...");

    // ── 单事务提交：删旧向量 → 写清单 → 插新向量 → 写指纹 ──
    const int64_t now = nowEpochSeconds();
    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        SQLite::Database& db = s.db();
        // 向量表维度（以本批嵌入为准；首启建表/维度变更在此完成）
        s.ensureVectorTable(static_cast<int>(embeddings.front().size()));

        for (auto& [key, ps] : desired) {
            const auto it = prevs.find(key);
            if (it != prevs.end()) {
                deleteSourceVectorsAndManifest(db, key, it->second);
            }
            SQLite::Statement ins_src(db,
                "INSERT INTO index_sources (source_key, content_hash, updated_at)"
                " VALUES (?, ?, ?)");
            ins_src.bind(1, key);
            ins_src.bind(2, ps.content_hash);
            ins_src.bind(3, static_cast<long long>(now));
            ins_src.exec();

            SQLite::Statement ins_chunk(db,
                "INSERT INTO index_chunks (source_key, chunk_id) VALUES (?, ?)");
            for (const auto& c : ps.chunks) {
                ins_chunk.bind(1, key);
                ins_chunk.bind(2, c.id);
                ins_chunk.exec();
                ins_chunk.reset();  // 复用 Statement 需复位：exec 后 mbDone=true，二次 exec 会抛 SQLITE_MISUSE
            }
        }

        // 覆盖清理：vec0 无 UNIQUE 约束，save_memory 等直达路径只插向量不写
        // 清单（deleteSourceVectorsAndManifest 管不到），先对本次 desired 的
        // 全部 chunk_id 逐条 DELETE 再插入，避免同 id 重复行累积成永久孤儿。
        {
            SQLite::Statement del_vec(db, "DELETE FROM vec_chunks WHERE chunk_id = ?");
            for (size_t i = 0; i < chunk_owner.size(); ++i) {
                const auto& [key, ci] = chunk_owner[i];
                del_vec.bind(1, desired[key].chunks[ci].id);
                del_vec.exec();
                del_vec.reset();  // 复用 Statement 需复位：exec 后 mbDone=true，二次 exec 会抛 SQLITE_MISUSE
            }
        }

        SQLite::Statement ins_vec(db,
            "INSERT INTO vec_chunks (chunk_id, metadata, embedding)"
            " VALUES (?, ?, ?)");
        for (size_t i = 0; i < chunk_owner.size(); ++i) {
            const auto& [key, ci] = chunk_owner[i];
            const auto& chunk = desired[key].chunks[ci];
            const auto& emb = embeddings[i];
            ins_vec.bind(1, chunk.id);
            ins_vec.bind(2, chunk.metadata.dump());
            nlohmann::json j = emb;
            ins_vec.bind(3, j.dump());
            ins_vec.exec();
            ins_vec.reset();  // 复用 Statement 需复位：exec 后 mbDone=true，二次 exec 会抛 SQLITE_MISUSE
        }

        writeFingerprint(s, model, embeddings.front().size());
    });

    report("向量索引已更新: " + std::to_string(result.total_chunks) + " 个片段 ("
         + std::to_string(result.updated_sources) + " 个源) → "
         + project_path + "/.novelagent/novel.db");
    return result;
}

} // namespace agent