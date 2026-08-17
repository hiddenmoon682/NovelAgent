// SqliteStore 与 sqlite-vec vec0 能力回归测试。

#include "storage/SqliteStore.h"

// ProjectIndexService 多 chunk 回归测试所需（core 内模块，与本测试同一预链接库）。
#include "agent/index/ProjectIndexService.h"
#include "project/Models/Project.h"
#include "project/ProjectAccess.h"
#include "project/ProjectIO.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "retrieval/NovelChunker.h"

#include "utils/FileUtils.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
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
    TEST("vec0 — 附加列读写（chunk_id/metadata）");
    const std::string db_path = tmpPath("tmp_test_vec0_additional.db");
    cleanup(db_path);

    storage::SqliteStore store;
    store.open(db_path);
    store.withLock([&](storage::SqliteStore& s) {
        s.ensureVectorTable(4);
        SQLite::Database& db = s.db();

        // 插入：附加列 + JSON 形式绑定向量
        SQLite::Statement ins(db,
            "INSERT INTO vec_chunks(chunk_id, metadata, embedding) "
            "VALUES(?, ?, ?)");
        ins.bind(1, "ch-001-seg-0");
        ins.bind(2, "{\"type\":\"chapter\"}");
        ins.bind(3, "[0.1,0.2,0.3,0.4]");
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
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding) "
                "VALUES(?, '{}', ?)");
            ins.bind(1, id); ins.bind(2, vec);
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
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding) "
                "VALUES('old', '{}', '[0.1,0.2,0.3,0.4]')");
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
                "INSERT INTO vec_chunks(chunk_id, metadata, embedding) "
                "VALUES('keep-1', '{}', '[0.1,0.2,0.3,0.4]')");
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

// ── ProjectIndexService 回归：多 chunk 章节（Statement 循环复用需 reset）──

// fake 嵌入生成器（固定 4 维向量，无网络依赖）。
// 模型名支持构造注入与 setModel 更换：向量行数不变的前提下换模型名，
// 用于验证 indexAll 的模型指纹失效重建路径（test_index_all_fingerprint_rebuild）。
class FakeEmbeddingGenerator : public retrieval::IEmbeddingGenerator {
public:
    explicit FakeEmbeddingGenerator(std::string model = "fake-embedding")
        : model_(std::move(model)) {}
    void setModel(std::string model) { model_ = std::move(model); }

    retrieval::EmbeddingVector generateEmbedding(const std::string&) override {
        return retrieval::EmbeddingVector(4, 1.0f);
    }
    std::vector<retrieval::EmbeddingVector> generateEmbeddings(
        const std::vector<std::string>& texts) override {
        return std::vector<retrieval::EmbeddingVector>(texts.size(),
                                                       retrieval::EmbeddingVector(4, 1.0f));
    }
    int dimension() const override { return 4; }
    std::string modelName() const override { return model_; }

private:
    std::string model_;
};

void test_index_all_multi_chunk() {
    TEST("ProjectIndexService — 多 chunk 章节索引与二次增量跳过");
    const std::string db_path = tmpPath("tmp_test_index_all.db");
    cleanup(db_path);
    const std::string proj_path =
        (std::filesystem::temp_directory_path() / "tmp_index_project").string();
    std::filesystem::remove_all(proj_path);
    std::filesystem::create_directories(proj_path + "/chapters");

    // 章节正文 >2000 字（chunker 默认 max 2000）→ 必然切分为 ≥2 个 chunk，
    // 若 ins_chunk/ins_vec 复用未 reset，二次 exec 抛 SQLITE_MISUSE 整体回滚。
    std::string content;
    const std::string sentence =
        "少年在雨夜推开了客栈的门，烛火在风里摇晃，掌柜抬起浑浊的眼睛看了他一眼。"
        "旧日的传闻在青石巷口重新响起，仿佛有人一遍遍念着那个被遗忘的名字。";
    for (int i = 0; i < 7; ++i) {
        for (int k = 0; k < 7; ++k) content += sentence;
        content += "\n\n";
    }
    ProjectIO::writeChapter(proj_path, "chapters/ch-001.md", content);

    auto proj = std::make_shared<Project>();
    proj->path = proj_path;
    proj->title = "索引回归测试";
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    ch.file_path = "chapters/ch-001.md";
    proj->outline.chapters.push_back(ch);

    storage::SqliteStore store;
    store.open(db_path);
    FakeEmbeddingGenerator gen;
    agent::ProjectIndexService svc(std::make_shared<ProjectAccess>(proj), store, gen);

    auto r1 = svc.indexAll();
    CHECK(r1.ok());
    CHECK(r1.chapters == 1);
    CHECK(r1.total_chunks >= 2);   // 多 chunk 章节不再因 Statement 复用而回滚
    CHECK(r1.updated_sources == 1);

    int vec_rows = -1, chunk_rows = -1, src_rows = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c1(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c1.executeStep();
        vec_rows = c1.getColumn(0).getInt();
        SQLite::Statement c2(s.db(), "SELECT COUNT(*) FROM index_chunks");
        c2.executeStep();
        chunk_rows = c2.getColumn(0).getInt();
        SQLite::Statement c3(s.db(), "SELECT COUNT(*) FROM index_sources");
        c3.executeStep();
        src_rows = c3.getColumn(0).getInt();
    });
    CHECK(vec_rows == r1.total_chunks);   // 每 chunk 一条向量
    CHECK(chunk_rows == r1.total_chunks); // 每 chunk 一清单行
    CHECK(src_rows == 1);

    // 第二次索引：内容哈希未变 → 全跳过，向量数不变
    auto r2 = svc.indexAll();
    CHECK(r2.ok());
    CHECK(r2.skipped_sources == 1);
    CHECK(r2.updated_sources == 0);
    int vec_rows2 = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        vec_rows2 = c.getColumn(0).getInt();
    });
    CHECK(vec_rows2 == vec_rows);

    store.close();
    cleanup(db_path);
    std::filesystem::remove_all(proj_path);
    PASS();
}

// ── ProjectIndexService 回归：覆盖清理 save_memory 直达写入的同 chunk_id 向量 ──

void test_index_all_dedup_direct_rows() {
    TEST("ProjectIndexService — 批量写入先删后插，直达向量（无清单记录）不产生重复行");
    const std::string db_path = tmpPath("tmp_test_index_dedup.db");
    cleanup(db_path);
    const std::string proj_path =
        (std::filesystem::temp_directory_path() / "tmp_index_dedup_project").string();
    std::filesystem::remove_all(proj_path);
    std::filesystem::create_directories(proj_path + "/chapters");

    // 章节正文 >2000 字 → 必然切分为 ≥2 个 chunk
    std::string content;
    const std::string sentence =
        "巷口的老灯笼在风里摇晃，守夜人把昨天的故事又讲了一遍，没有人记得名字。";
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 6; ++k) content += sentence;
        content += "\n\n";
    }
    ProjectIO::writeChapter(proj_path, "chapters/ch-001.md", content);

    auto proj = std::make_shared<Project>();
    proj->path = proj_path;
    proj->title = "索引去重回归测试";
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    ch.file_path = "chapters/ch-001.md";
    proj->outline.chapters.push_back(ch);

    storage::SqliteStore store;
    store.open(db_path);
    FakeEmbeddingGenerator gen;

    // 用与 indexAll 相同的 chunker 配置推导首个 chunk 的 id（避免硬编码格式漂移）
    retrieval::NovelChunker probe;
    const auto probe_chunks = probe.chunkChapter(ch, content);
    CHECK(probe_chunks.size() >= 2);
    const std::string first_id = probe_chunks.front().id;

    // 模拟 save_memory 直达路径：仅插向量、不写清单。vec0 无 UNIQUE 约束，
    // 若批量写入不先删旧向量，同 chunk_id 将累积重复行（迁移回归）。
    store.withLock([&](storage::SqliteStore& s) {
        s.ensureVectorTable(gen.dimension());  // 与 fake 生成器维度一致，防 DROP
        SQLite::Statement ins(s.db(),
            "INSERT INTO vec_chunks(chunk_id, metadata, embedding) "
            "VALUES(?, '{}', '[0.1,0.2,0.3,0.4]')");
        ins.bind(1, first_id);
        ins.exec();
    });

    agent::ProjectIndexService svc(std::make_shared<ProjectAccess>(proj), store, gen);

    auto r1 = svc.indexAll();
    CHECK(r1.ok());
    CHECK(r1.total_chunks >= 2);

    int vec_rows = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        vec_rows = c.getColumn(0).getInt();
    });
    // 重复行被先删后插覆盖：行数 == chunk 总数（旧实现无 DELETE → 多 1 行，red）
    CHECK(vec_rows == r1.total_chunks);

    // 第二次索引：内容哈希未变 → 全跳过，行数仍不变
    auto r2 = svc.indexAll();
    CHECK(r2.ok());
    CHECK(r2.skipped_sources == 1);
    CHECK(r2.updated_sources == 0);
    int vec_rows2 = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        vec_rows2 = c.getColumn(0).getInt();
    });
    CHECK(vec_rows2 == vec_rows);

    store.close();
    cleanup(db_path);
    std::filesystem::remove_all(proj_path);
    PASS();
}

// ── ProjectIndexService 回归：模型指纹失效整库重建 ──

void test_index_all_fingerprint_rebuild() {
    TEST("ProjectIndexService — 换模型名触发整库失效重建，向量行数不变（无重复）");
    const std::string db_path = tmpPath("tmp_test_index_fingerprint.db");
    cleanup(db_path);
    const std::string proj_path =
        (std::filesystem::temp_directory_path() / "tmp_index_fingerprint_project").string();
    std::filesystem::remove_all(proj_path);
    std::filesystem::create_directories(proj_path + "/chapters");

    // 章节正文 >2000 字 → 必然切分为 ≥2 个 chunk
    std::string content;
    const std::string sentence =
        "雨后的山路上，赶路人捡起一封没有署名的信，字迹被水洇得模糊，却依稀能认出自己的名字。";
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 6; ++k) content += sentence;
        content += "\n\n";
    }
    ProjectIO::writeChapter(proj_path, "chapters/ch-001.md", content);

    auto proj = std::make_shared<Project>();
    proj->path = proj_path;
    proj->title = "指纹重建回归测试";
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    ch.file_path = "chapters/ch-001.md";
    proj->outline.chapters.push_back(ch);

    storage::SqliteStore store;
    store.open(db_path);
    FakeEmbeddingGenerator gen;
    agent::ProjectIndexService svc(std::make_shared<ProjectAccess>(proj), store, gen);

    // 首次索引：全量写入模型指纹
    auto r1 = svc.indexAll();
    CHECK(r1.ok());
    CHECK(r1.updated_sources == 1);
    CHECK(r1.total_chunks >= 2);
    int vec_rows1 = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        vec_rows1 = c.getColumn(0).getInt();
    });
    CHECK(vec_rows1 == r1.total_chunks);

    // 换模型名 → kv 指纹不匹配 → 整库失效重建：全部源重嵌
    // （updated==源数、skipped==0）、向量行数仍等于 chunk 数（无重复累积），
    // 指纹已更新为新模型
    gen.setModel("fake-embedding-v2");
    auto r2 = svc.indexAll();
    CHECK(r2.ok());
    CHECK(r2.updated_sources == 1);
    CHECK(r2.skipped_sources == 0);
    int vec_rows2 = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        vec_rows2 = c.getColumn(0).getInt();
        CHECK(s.getKV("embedding_model") == "fake-embedding-v2");
    });
    CHECK(vec_rows2 == vec_rows1);

    // 同模型再跑一次：全部跳过、行数不变（指纹已固化，不再触发重建）
    auto r3 = svc.indexAll();
    CHECK(r3.ok());
    CHECK(r3.skipped_sources == 1);
    CHECK(r3.updated_sources == 0);

    store.close();
    cleanup(db_path);
    std::filesystem::remove_all(proj_path);
    PASS();
}

// ── ProjectIndexService 回归：大纲移除章节后的孤儿向量清理 ──

void test_index_all_orphan_cleanup() {
    TEST("ProjectIndexService — 章节移出大纲后，下次索引清理其孤儿向量");
    const std::string db_path = tmpPath("tmp_test_index_orphan.db");
    cleanup(db_path);
    const std::string proj_path =
        (std::filesystem::temp_directory_path() / "tmp_index_orphan_project").string();
    std::filesystem::remove_all(proj_path);
    std::filesystem::create_directories(proj_path + "/chapters");

    // 两章均 >2000 字 → 均为多 chunk；正文不同，避免 chunk_id 与哈希歧义
    auto make_content = [&](const std::string& line) {
        std::string content;
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 6; ++k) content += line;
            content += "\n\n";
        }
        return content;
    };
    const std::string line1 = "暮色四合时，客栈的掌柜在柜台上点亮了油灯，把最后一位客人让进屋里。";
    const std::string line2 = "清晨的码头，船工解开缆绳，货郎挑着担子挤进第一班渡船。";
    const std::string content1 = make_content(line1);
    ProjectIO::writeChapter(proj_path, "chapters/ch-001.md", content1);
    ProjectIO::writeChapter(proj_path, "chapters/ch-002.md", make_content(line2));

    auto proj = std::make_shared<Project>();
    proj->path = proj_path;
    proj->title = "孤儿清理回归测试";
    Chapter ch1;
    ch1.id = "ch-001"; ch1.title = "第一章"; ch1.file_path = "chapters/ch-001.md";
    Chapter ch2;
    ch2.id = "ch-002"; ch2.title = "第二章"; ch2.file_path = "chapters/ch-002.md";
    proj->outline.chapters.push_back(ch1);
    proj->outline.chapters.push_back(ch2);

    storage::SqliteStore store;
    store.open(db_path);
    FakeEmbeddingGenerator gen;
    agent::ProjectIndexService svc(std::make_shared<ProjectAccess>(proj), store, gen);

    // 首次索引：两个章节源全部入库
    auto r1 = svc.indexAll();
    CHECK(r1.ok());
    CHECK(r1.chapters == 2);
    CHECK(r1.updated_sources == 2);
    CHECK(r1.total_chunks >= 4);
    int vec_rows1 = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c(s.db(), "SELECT COUNT(*) FROM vec_chunks");
        c.executeStep();
        vec_rows1 = c.getColumn(0).getInt();
    });
    CHECK(vec_rows1 == r1.total_chunks);

    // 从大纲移除 ch-002（索引源以 outline 为准，正文文件去留无关）
    proj->outline.chapters.erase(
        std::remove_if(proj->outline.chapters.begin(), proj->outline.chapters.end(),
                       [](const Chapter& c) { return c.id == "ch-002"; }),
        proj->outline.chapters.end());

    // 下次索引：ch-002 清单在但源已删除 → 清理其向量与清单；ch-001 哈希未变则跳过
    auto r2 = svc.indexAll();
    CHECK(r2.ok());
    CHECK(r2.removed_sources == 1);
    CHECK(r2.skipped_sources == 1);
    CHECK(r2.updated_sources == 0);

    // 剩余向量数 == ch-001 单独切分的 chunk 数；ch-002 相关向量已不存在
    retrieval::NovelChunker probe;
    const size_t expect_ch001 = probe.chunkChapter(ch1, content1).size();
    int vec_ch001 = -1, vec_ch002 = -1, src_count = -1;
    store.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement c1(s.db(), "SELECT COUNT(*) FROM vec_chunks WHERE chunk_id LIKE 'ch-001-%'");
        c1.executeStep();
        vec_ch001 = c1.getColumn(0).getInt();
        SQLite::Statement c2(s.db(), "SELECT COUNT(*) FROM vec_chunks WHERE chunk_id LIKE 'ch-002-%'");
        c2.executeStep();
        vec_ch002 = c2.getColumn(0).getInt();
        SQLite::Statement c3(s.db(), "SELECT COUNT(*) FROM index_sources");
        c3.executeStep();
        src_count = c3.getColumn(0).getInt();
    });
    CHECK(vec_ch001 == static_cast<int>(expect_ch001));  // ch-001 向量完整保留
    CHECK(vec_ch002 == 0);                               // ch-002 孤儿向量已清理
    CHECK(src_count == 1);                               // 清单同样收敛到 1 个源

    store.close();
    cleanup(db_path);
    std::filesystem::remove_all(proj_path);
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
    test_index_all_multi_chunk();
    test_index_all_dedup_direct_rows();
    test_index_all_fingerprint_rebuild();
    test_index_all_orphan_cleanup();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}