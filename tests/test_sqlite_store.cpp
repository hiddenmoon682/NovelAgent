// SqliteStore 与 sqlite-vec vec0 能力回归测试。

#include "storage/SqliteStore.h"

#include "utils/FileUtils.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() \
    do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); } } while(0)

static std::string tmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

static void cleanup(const std::string& path) {
    for (const auto& suffix : {"", "-wal", "-shm"}) {
        const std::string p = path + suffix;
        if (utils::file::exists(p)) utils::file::removeFile(p);
    }
    // 一并清理损坏自愈备份（<基名>.corrupt-*），避免残留。
    // corrupt 文件名格式为 <基名>.corrupt-<时间戳>；目录项 filename() 只含
    // 文件名（不含目录），前缀必须用基名构造，不能用含目录的完整 path。
    const std::string base = std::filesystem::path(path).filename().string();
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(
             std::filesystem::temp_directory_path(), ec)) {
        if (e.path().filename().string().find(base + ".corrupt-") == 0)
            std::filesystem::remove(e.path(), ec);
    }
}

// ── open/close/建表 ──

void test_open_close() {
    TEST("SqliteStore — open/close/isOpen/path");
    const std::string db_path = tmpPath("tmp_test_store_open.db");
    cleanup(db_path);

    storage::SqliteStore store;
    CHECK(!store.isOpen());
    store.open(db_path);
    CHECK(store.isOpen());
    CHECK(store.path() == db_path);
    store.close();
    CHECK(!store.isOpen());

    cleanup(db_path);
    PASS();
}

void test_schema_tables_exist() {
    TEST("SqliteStore — 建表迁移（业务表齐全）");
    const std::string db_path = tmpPath("tmp_test_store_schema.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
        std::vector<std::string> tables;
        while (stmt.executeStep()) tables.push_back(stmt.getColumn(0).getString());
        for (const char* expect : {"sessions", "messages", "message_history",
                                   "index_sources", "index_chunks", "kv_store"}) {
            CHECK(std::find(tables.begin(), tables.end(), expect) != tables.end());
        }
    });
    store.close();
    cleanup(db_path);
    PASS();
}

void test_wal_and_foreign_keys() {
    TEST("SqliteStore — WAL 与外键开启");
    const std::string db_path = tmpPath("tmp_test_store_wal.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement jm(s.db(), "PRAGMA journal_mode");
        CHECK(jm.executeStep() && jm.getColumn(0).getString() == "wal");
        SQLite::Statement fk(s.db(), "PRAGMA foreign_keys");
        CHECK(fk.executeStep() && fk.getColumn(0).getInt() == 1);
    });
    store.close();
    cleanup(db_path);
    PASS();
}

void test_transaction_rollback() {
    TEST("SqliteStore — 事务回滚（回调抛异常不落库）");
    const std::string db_path = tmpPath("tmp_test_store_txn.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    bool threw = false;
    try {
        store.inTransaction([&](storage::SqliteStore& s) {
            s.setKV("k", "v");
            throw std::runtime_error("boom");
        });
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    int kv_count = 0;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(), "SELECT COUNT(*) FROM kv_store");
        stmt.executeStep();
        kv_count = stmt.getColumn(0).getInt();
    });
    CHECK(kv_count == 0);  // 回滚后无残留
    store.close();
    cleanup(db_path);
    PASS();
}

void test_corrupt_recover() {
    TEST("SqliteStore — 损坏自愈（改名 .corrupt-* 并重建空库）");
    const std::string db_path = tmpPath("tmp_test_store_corrupt.db");
    cleanup(db_path);

    // 写入垃圾字节模拟损坏库
    utils::file::writeText(db_path, "this is not a sqlite database at all");

    storage::SqliteStore store;
    store.open(db_path);
    CHECK(store.isOpen());  // 自愈后可用
    int n = 0;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(), "SELECT COUNT(*) FROM sessions");
        stmt.executeStep();
        n = stmt.getColumn(0).getInt();
    });
    CHECK(n == 0);
    store.close();

    // 原文件应已被改名
    bool found_corrupt = false;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(
             std::filesystem::temp_directory_path(), ec)) {
        if (e.path().filename().string().find("tmp_test_store_corrupt.db.corrupt-") == 0)
            found_corrupt = true;
    }
    CHECK(found_corrupt);

    cleanup(db_path);
    PASS();
}

// ── vec0 虚拟表能力（spike 核验，锁定用法）──

void test_vec0_additional_columns() {
    TEST("vec0 — 附加列读写（chunk_id/metadata/embedding_json + embedding）");
    const std::string db_path = tmpPath("tmp_test_vec0_additional.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        s.ensureVectorTable(4);
        SQLite::Database& db = s.db();

        // 插入：附加列 + JSON 形式绑定向量
        SQLite::Statement ins(db,
            "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
            "VALUES(?, ?, ?, ?)");
        ins.bind(1, "ch-001-seg-0");
        ins.bind(2, "{\"type\":\"chapter\"}");
        ins.bind(3, "[0.1,0.2,0.3,0.4]");
        ins.bind(4, "[0.1,0.2,0.3,0.4]");
        ins.exec();

        // 附加列可查询（kNN 返回时携带）
        SQLite::Statement q(db,
            "SELECT chunk_id, metadata, distance FROM vec_chunks "
            "WHERE embedding MATCH '[0.1,0.2,0.3,0.4]' AND k = 5");
        CHECK(q.executeStep());
        CHECK(q.getColumn(0).getString() == "ch-001-seg-0");
        CHECK(q.getColumn(1).getString() == "{\"type\":\"chapter\"}");
    });
    store.close();
    cleanup(db_path);
    PASS();
}

void test_vec0_cosine_mapping() {
    TEST("vec0 — cosine 距离与 similarity=1-d/2 映射");
    const std::string db_path = tmpPath("tmp_test_vec0_cosine.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        s.ensureVectorTable(2);
        SQLite::Database& db = s.db();

        auto insert = [&](const std::string& id, const std::string& vec) {
            SQLite::Statement ins(db,
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
                "VALUES(?, '{}', ?, ?)");
            ins.bind(1, id); ins.bind(2, vec); ins.bind(3, vec);
            ins.exec();
        };
        insert("same", "[1.0,1.0]");
        insert("opp", "[-1.0,-1.0]");

        SQLite::Statement q(db,
            "SELECT chunk_id, distance FROM vec_chunks "
            "WHERE embedding MATCH '[1.0,1.0]' AND k = 2");
        int found = 0;
        while (q.executeStep()) {
            const std::string id = q.getColumn(0).getString();
            const double d = q.getColumn(1).getDouble();
            if (id == "same") {
                // 同向：cos=1 → distance 0 → similarity 1
                CHECK(d < 1e-6);
                CHECK(std::abs(1.0 - d / 2.0 - 1.0) < 1e-6);
            } else if (id == "opp") {
                // 反向：cos=-1 → distance 2 → similarity 0
                CHECK(std::abs(d - 2.0) < 1e-6);
                CHECK(std::abs(1.0 - d / 2.0 - 0.0) < 1e-6);
            }
            ++found;
        }
        CHECK(found == 2);
    });
    store.close();
    cleanup(db_path);
    PASS();
}

void test_vec0_drop_recreate_in_txn() {
    TEST("vec0 — 事务内 DROP + 重建（维度变更路径）");
    const std::string db_path = tmpPath("tmp_test_vec0_recreate.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        s.ensureVectorTable(4);
        {
            SQLite::Statement ins(s.db(),
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
                "VALUES('old', '{}', '[0.1,0.2,0.3,0.4]', '[0.1,0.2,0.3,0.4]')");
            ins.exec();
        }
        // 重置维度 → DROP 重建
        s.ensureVectorTable(8);
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        CHECK(c.getColumn(0).getInt() == 0);  // 旧数据已清空
    });
    store.close();
    cleanup(db_path);
    PASS();
}

void test_reopen_keeps_vector_data() {
    TEST("SqliteStore — 重启后同维度 ensureVectorTable 不丢向量（reopen 语义）");
    const std::string db_path = tmpPath("tmp_test_store_reopen.db");
    cleanup(db_path);

    {  // 首次会话：建库 + 建 vec_chunks(4) + 插入一条向量
        storage::SqliteStore store;
        store.open(db_path);
        store.withLock([&](storage::SqliteStore& s) {
            s.ensureVectorTable(4);
            SQLite::Statement ins(s.db(),
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding_json, embedding) "
                "VALUES('keep-1', '{}', '[0.1,0.2,0.3,0.4]', '[0.1,0.2,0.3,0.4]')");
            ins.exec();
        });
    }  // 析构即 close，模拟一次完整启停

    // 二次会话：open 应恢复维度缓存，同维度 ensureVectorTable 短路、不 DROP
    storage::SqliteStore store2;
    store2.open(db_path);
    CHECK(store2.vectorDimension() == 4);  // 维度从库内恢复，而非初始 0
    store2.withLock([&](storage::SqliteStore& s) {
        s.ensureVectorTable(4);
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        CHECK(c.getColumn(0).getInt() == 1);  // 既有向量仍在
    });
    store2.close();
    cleanup(db_path);
    PASS();
}

void test_kv_roundtrip() {
    TEST("kv_store — 读写往返（模型指纹等）");
    const std::string db_path = tmpPath("tmp_test_kv.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        CHECK(s.getKV("embedding_model") == "");
        s.setKV("embedding_model", "text-embedding-3-small");
        s.setKV("embedding_dimension", "1536");
        CHECK(s.getKV("embedding_model") == "text-embedding-3-small");
        CHECK(s.getKV("embedding_dimension") == "1536");
    });
    store.close();
    cleanup(db_path);
    PASS();
}

void test_index_tables_cascade() {
    TEST("index_sources/index_chunks — 级联删除与外键");
    const std::string db_path = tmpPath("tmp_test_store_cascade.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        {
            SQLite::Statement ins(s.db(),
                "INSERT INTO index_sources (source_key, content_hash, updated_at)"
                " VALUES ('chapter:ch-001', 'abc', 1)");
            ins.exec();
            SQLite::Statement ins2(s.db(),
                "INSERT INTO index_chunks (source_key, chunk_id) VALUES ('chapter:ch-001', 'ch-001-0')");
            ins2.exec();
        }
        {
            SQLite::Statement del(s.db(), "DELETE FROM index_sources WHERE source_key = 'chapter:ch-001'");
            del.exec();
        }
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM index_chunks");
        c.executeStep();
        CHECK(c.getColumn(0).getInt() == 0);  // 级联清空
    });
    store.close();
    cleanup(db_path);
    PASS();
}

int main() {
    std::cout << "=== test_sqlite_store ===\n\n";
    test_open_close();
    test_schema_tables_exist();
    test_wal_and_foreign_keys();
    test_transaction_rollback();
    test_corrupt_recover();
    test_vec0_additional_columns();
    test_vec0_cosine_mapping();
    test_vec0_drop_recreate_in_txn();
    test_reopen_keeps_vector_data();
    test_kv_roundtrip();
    test_index_tables_cascade();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}