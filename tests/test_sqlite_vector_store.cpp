// SqliteVectorStore — IVectorStore 全语义回归测试（对齐旧 JsonVectorStore 行为）。

#include "retrieval/SqliteVectorStore.h"
#include "retrieval/IVectorStore.h"
#include "storage/SqliteStore.h"
#include "utils/FileUtils.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
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
}

static std::vector<float> makeVec(int dim, float val) {
    return std::vector<float>(dim, val);
}

// 打开临时库并绑定 SqliteVectorStore。
struct Fixture {
    storage::SqliteStore db;
    retrieval::SqliteVectorStore store;
    explicit Fixture(const std::string& path) : store(db) { db.open(path); }
};

void test_insert_and_search() {
    TEST("SqliteVectorStore — 插入和搜索（相似度映射 [0,1] 降序）");
    const std::string db_path = tmpPath("tmp_test_svvs_search.db");
    cleanup(db_path);
    Fixture fx(db_path);

    fx.store.insert("id-1", makeVec(4, 1.0f), {{"label", "positive"}});
    fx.store.insert("id-2", makeVec(4, -1.0f), {{"label", "negative"}});
    fx.store.insert("id-3", makeVec(4, 0.5f), {{"label", "neutral"}});

    CHECK(fx.store.count() == 3);
    CHECK(fx.store.contains("id-1"));
    CHECK(!fx.store.contains("id-nonexistent"));

    auto results = fx.store.search(makeVec(4, 1.0f), 3);
    CHECK(results.size() == 3);
    CHECK(results[0].similarity > 0.99);
    CHECK(results[1].similarity > 0.99);
    CHECK((results[0].id == "id-1" && results[1].id == "id-3") ||
          (results[0].id == "id-3" && results[1].id == "id-1"));
    CHECK(results[2].id == "id-2");
    CHECK(results[1].similarity > results[2].similarity);
    // 元数据随结果返回并保留
    CHECK(results[0].metadata.contains("label"));

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_insert_overwrite() {
    TEST("SqliteVectorStore — insert 同 id 覆盖");
    const std::string db_path = tmpPath("tmp_test_svvs_overwrite.db");
    cleanup(db_path);
    Fixture fx(db_path);

    fx.store.insert("a", {1.0f, 0.0f}, {{"v", 1}});
    fx.store.insert("a", {0.0f, 1.0f}, {{"v", 2}});
    CHECK(fx.store.count() == 1);
    auto e = fx.store.get("a");
    CHECK(e.has_value());
    CHECK(e->metadata["v"] == 2);
    CHECK(std::abs(e->embedding[0] - 0.0f) < 1e-6f);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_persistence_across_reopen() {
    TEST("SqliteVectorStore — 持久化往返（重开库仍可读）");
    const std::string db_path = tmpPath("tmp_test_svvs_persist.db");
    cleanup(db_path);

    {
        Fixture fx(db_path);
        fx.store.insert("persist-1", {0.1f, 0.2f, 0.3f}, {{"key", "value1"}});
        fx.store.insert("persist-2", {0.4f, 0.5f, 0.6f}, {{"key", "value2"}});
        fx.db.close();
    }
    {
        Fixture fx(db_path);
        CHECK(fx.store.count() == 2);
        CHECK(fx.store.contains("persist-1"));
        auto entry = fx.store.get("persist-1");
        CHECK(entry.has_value());
        CHECK(entry->embedding.size() == 3);
        CHECK(std::abs(entry->embedding[0] - 0.1f) < 0.001f);
        CHECK(entry->metadata["key"] == "value1");
        fx.db.close();
    }
    cleanup(db_path);
    PASS();
}

void test_remove() {
    TEST("SqliteVectorStore — 删除向量");
    const std::string db_path = tmpPath("tmp_test_svvs_remove.db");
    cleanup(db_path);
    Fixture fx(db_path);

    fx.store.insert("rm-1", {1.0f, 2.0f}, {});
    fx.store.insert("rm-2", {3.0f, 4.0f}, {});
    CHECK(fx.store.count() == 2);
    CHECK(fx.store.remove("rm-1"));
    CHECK(fx.store.count() == 1);
    CHECK(!fx.store.contains("rm-1"));
    CHECK(!fx.store.remove("rm-nonexistent"));

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_update() {
    TEST("SqliteVectorStore — 更新向量（保留元数据）");
    const std::string db_path = tmpPath("tmp_test_svvs_update.db");
    cleanup(db_path);
    Fixture fx(db_path);

    fx.store.insert("up-1", {0.1f, 0.2f}, {{"keep", "me"}});
    fx.store.update("up-1", {0.9f, 0.8f});
    auto entry = fx.store.get("up-1");
    CHECK(entry.has_value());
    CHECK(std::abs(entry->embedding[0] - 0.9f) < 0.001f);
    CHECK(entry->metadata["keep"] == "me");

    // 更新不存在的 id → 等价 insert（空元数据）
    fx.store.update("up-2", {0.5f, 0.5f});
    CHECK(fx.store.count() == 2);
    auto e2 = fx.store.get("up-2");
    CHECK(e2.has_value());
    CHECK(e2->metadata.empty());

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_batch_insert() {
    TEST("SqliteVectorStore — 批量插入");
    const std::string db_path = tmpPath("tmp_test_svvs_batch.db");
    cleanup(db_path);
    Fixture fx(db_path);

    std::vector<retrieval::VectorEntry> entries;
    for (int i = 0; i < 10; ++i) {
        entries.push_back({"batch-" + std::to_string(i),
                           {static_cast<float>(i) * 0.1f, static_cast<float>(i) * 0.2f},
                           {{"index", i}}});
    }
    fx.store.insertBatch(entries);
    CHECK(fx.store.count() == 10);
    CHECK(fx.store.contains("batch-7"));

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_query_empty_id() {
    TEST("SqliteVectorStore — 空 top_k 返回空");
    const std::string db_path = tmpPath("tmp_test_svvs_empty.db");
    cleanup(db_path);
    Fixture fx(db_path);
    fx.store.insert("a", {0.1f, 0.1f}, {});
    CHECK(fx.store.search({0.1f, 0.1f}, 0).empty());
    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_search_metadata_roundtrip() {
    TEST("SqliteVectorStore — 复杂元数据 JSON 往返");
    const std::string db_path = tmpPath("tmp_test_svvs_meta.db");
    cleanup(db_path);
    Fixture fx(db_path);

    fx.store.insert("m-1", {0.5f, 0.5f},
                    {{"type", "memory"}, {"memory_id", "mem-1"}, {"kind", "fact"}});
    auto r = fx.store.search({0.5f, 0.5f}, 1);
    CHECK(r.size() == 1);
    CHECK(r[0].metadata["type"] == "memory");
    CHECK(r[0].metadata["memory_id"] == "mem-1");

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_dimension_mismatch_recreates() {
    TEST("SqliteVectorStore — 维度变更透明重建（旧数据清空）");
    const std::string db_path = tmpPath("tmp_test_svvs_dim.db");
    cleanup(db_path);
    Fixture fx(db_path);

    fx.store.insert("old", {0.1f, 0.2f, 0.3f, 0.4f}, {});
    CHECK(fx.store.count() == 1);
    // 换维度写入 → DROP 重建 → 旧数据清空
    fx.store.insert("new", {0.1f, 0.2f}, {});
    CHECK(fx.store.count() == 1);
    CHECK(fx.store.contains("new"));
    CHECK(!fx.store.contains("old"));

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_concurrent_read_write() {
    TEST("SqliteVectorStore — 并发写入/搜索无竞争");
    const std::string db_path = tmpPath("tmp_test_svvs_conc.db");
    cleanup(db_path);
    Fixture fx(db_path);

    constexpr int kWriters = 2;
    constexpr int kPerWriter = 50;
    std::vector<std::thread> threads;
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&fx, w]() {
            for (int i = 0; i < kPerWriter; ++i) {
                fx.store.insert("conc-" + std::to_string(w) + "-" + std::to_string(i),
                                {static_cast<float>(w), static_cast<float>(i)}, {});
            }
        });
    }
    threads.emplace_back([&fx]() {
        for (int i = 0; i < 20; ++i) {
            (void)fx.store.search({1.0f, 1.0f}, 5);
            (void)fx.store.count();
            fx.store.flush();  // no-op 不抛异常
        }
    });
    for (auto& t : threads) t.join();

    CHECK(fx.store.count() == kWriters * kPerWriter);

    // 重开库验证落盘完整
    fx.db.close();
    Fixture fx2(db_path);
    CHECK(fx2.store.count() == kWriters * kPerWriter);
    fx2.db.close();
    cleanup(db_path);
    PASS();
}

int main() {
    std::cout << "=== test_sqlite_vector_store ===\n\n";
    test_insert_and_search();
    test_insert_overwrite();
    test_persistence_across_reopen();
    test_remove();
    test_update();
    test_batch_insert();
    test_query_empty_id();
    test_search_metadata_roundtrip();
    test_dimension_mismatch_recreates();
    test_concurrent_read_write();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}