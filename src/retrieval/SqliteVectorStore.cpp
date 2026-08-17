// SqliteVectorStore 实现 — vec0 虚拟表 CRUD + kNN 检索。

#include "retrieval/SqliteVectorStore.h"

#include "storage/SqliteStore.h"

#include <SQLiteCpp/Statement.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace retrieval {

namespace {

// 向量 → JSON 数组字符串（sqlite-vec MATCH/绑定接受 JSON 形式）。
std::string vecToJson(const EmbeddingVector& v)
{
    nlohmann::json j = v;
    return j.dump();
}

// 从 embedding_json 列还原向量。
EmbeddingVector parseEmbeddingJson(const std::string& s)
{
    EmbeddingVector out;
    try {
        auto j = nlohmann::json::parse(s);
        if (!j.is_array()) return out;
        for (const auto& val : j) {
            if (val.is_number()) out.push_back(val.get<float>());
        }
    } catch (...) {
        // 列损坏时返回空向量（与旧实现 load 防御一致）
    }
    return out;
}

} // namespace

void SqliteVectorStore::insert(
    const std::string& id,
    const EmbeddingVector& embedding,
    const nlohmann::json& metadata)
{
    if (!store_.isOpen() || embedding.empty()) return;
    store_.inTransaction([&](storage::SqliteStore& s) {
        s.ensureVectorTable(static_cast<int>(embedding.size()));
        SQLite::Database& db = s.db();

        // vec0 无原生 UPSERT：先删后插，覆盖语义与旧实现一致
        {
            SQLite::Statement del(db, "DELETE FROM vec_chunks WHERE chunk_id = ?");
            del.bind(1, id);
            del.exec();
        }
        SQLite::Statement ins(db,
            "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
            "VALUES(?, ?, ?, ?)");
        ins.bind(1, id);
        ins.bind(2, metadata.dump());
        ins.bind(3, vecToJson(embedding));
        ins.bind(4, vecToJson(embedding));
        ins.exec();
    });
}

void SqliteVectorStore::insertBatch(const std::vector<VectorEntry>& entries)
{
    if (!store_.isOpen() || entries.empty()) return;
    std::vector<VectorEntry> non_empty;
    for (const auto& e : entries) {
        if (!e.embedding.empty()) non_empty.push_back(e);
    }
    if (non_empty.empty()) return;

    store_.inTransaction([&](storage::SqliteStore& s) {
        s.ensureVectorTable(static_cast<int>(non_empty.front().embedding.size()));
        SQLite::Database& db = s.db();
        for (const auto& entry : non_empty) {
            {
                SQLite::Statement del(db, "DELETE FROM vec_chunks WHERE chunk_id = ?");
                del.bind(1, entry.id);
                del.exec();
            }
            SQLite::Statement ins(db,
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
                "VALUES(?, ?, ?, ?)");
            ins.bind(1, entry.id);
            ins.bind(2, entry.metadata.dump());
            ins.bind(3, vecToJson(entry.embedding));
            ins.bind(4, vecToJson(entry.embedding));
            ins.exec();
        }
    });
    spdlog::debug("[SqliteVectorStore] 批量插入 {} 条向量", non_empty.size());
}

bool SqliteVectorStore::remove(const std::string& id)
{
    if (!store_.isOpen()) return false;
    return store_.inTransaction([&](storage::SqliteStore& s) -> bool {
        SQLite::Database& db = s.db();
        // 先确认存在再删（避免依赖 exec() 的变更计数返回值）
        {
            SQLite::Statement q(db, "SELECT 1 FROM vec_chunks WHERE chunk_id = ?");
            q.bind(1, id);
            if (!q.executeStep()) return false;
        }
        SQLite::Statement del(db, "DELETE FROM vec_chunks WHERE chunk_id = ?");
        del.bind(1, id);
        del.exec();
        return true;
    });
}

void SqliteVectorStore::update(const std::string& id, const EmbeddingVector& embedding)
{
    if (!store_.isOpen() || embedding.empty()) return;
    store_.inTransaction([&](storage::SqliteStore& s) {
        s.ensureVectorTable(static_cast<int>(embedding.size()));
        SQLite::Database& db = s.db();

        // 保留既有元数据；id 不存在时以空元数据新建（与旧实现一致）
        nlohmann::json meta = nlohmann::json::object();
        {
            SQLite::Statement q(db,
                "SELECT metadata FROM vec_chunks WHERE chunk_id = ?");
            q.bind(1, id);
            if (q.executeStep()) {
                try {
                    meta = nlohmann::json::parse(q.getColumn(0).getString());
                } catch (...) {}
            }
        }
        {
            SQLite::Statement del(db, "DELETE FROM vec_chunks WHERE chunk_id = ?");
            del.bind(1, id);
            del.exec();
        }
        SQLite::Statement ins(db,
            "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
            "VALUES(?, ?, ?, ?)");
        ins.bind(1, id);
        ins.bind(2, meta.dump());
        ins.bind(3, vecToJson(embedding));
        ins.bind(4, vecToJson(embedding));
        ins.exec();
    });
}

std::vector<SearchResult> SqliteVectorStore::search(
    const EmbeddingVector& query_embedding,
    int top_k) const
{
    if (!store_.isOpen() || query_embedding.empty() || top_k <= 0) return {};
    return store_.withLock([&](storage::SqliteStore& s) -> std::vector<SearchResult> {
        SQLite::Statement stmt(s.db(),
            "SELECT chunk_id, metadata, distance FROM vec_chunks "
            "WHERE embedding MATCH ? AND k = ?");
        stmt.bind(1, vecToJson(query_embedding));
        stmt.bind(2, top_k);

        std::vector<SearchResult> out;
        while (stmt.executeStep()) {
            SearchResult r;
            r.id = stmt.getColumn(0).getString();
            try {
                r.metadata = nlohmann::json::parse(stmt.getColumn(1).getString());
            } catch (...) {
                r.metadata = nlohmann::json::object();
            }
            const double d = stmt.getColumn(2).getDouble();
            r.similarity = 1.0 - d / 2.0;
            out.push_back(std::move(r));
        }
        return out;
    });
}

int SqliteVectorStore::count() const
{
    if (!store_.isOpen()) return 0;
    return store_.withLock([&](storage::SqliteStore& s) -> int {
        SQLite::Statement stmt(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        stmt.executeStep();
        return stmt.getColumn(0).getInt();
    });
}

bool SqliteVectorStore::contains(const std::string& id) const
{
    if (!store_.isOpen()) return false;
    return store_.withLock([&](storage::SqliteStore& s) -> bool {
        SQLite::Statement stmt(s.db(),
            "SELECT 1 FROM vec_chunks WHERE chunk_id = ? LIMIT 1");
        stmt.bind(1, id);
        return stmt.executeStep();
    });
}

std::optional<VectorEntry> SqliteVectorStore::get(const std::string& id) const
{
    if (!store_.isOpen()) return std::nullopt;
    return store_.withLock([&](storage::SqliteStore& s) -> std::optional<VectorEntry> {
        SQLite::Statement stmt(s.db(),
            "SELECT chunk_id, metadata, embedding_json FROM vec_chunks WHERE chunk_id = ?");
        stmt.bind(1, id);
        if (!stmt.executeStep()) return std::nullopt;
        VectorEntry e;
        e.id = stmt.getColumn(0).getString();
        try {
            e.metadata = nlohmann::json::parse(stmt.getColumn(1).getString());
        } catch (...) {
            e.metadata = nlohmann::json::object();
        }
        e.embedding = parseEmbeddingJson(stmt.getColumn(2).getString());
        return e;
    });
}

} // namespace retrieval