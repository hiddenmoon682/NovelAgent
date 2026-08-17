# SQLite 单库存储迁移实施计划（Phase 1）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将会话/消息、向量索引、索引清单从 JSON 文件迁入 `.novelagent/novel.db`（SQLite + sqlite-vec），删除旧 `VectorStore`/`IndexManifest` 与旧文件布局，全量测试通过。

**Architecture:** 新增 `third_party/`（sqlite3 amalgamation + sqlite-vec + SQLiteCpp）与 `src/storage/SqliteStore`（单连接、全库互斥锁、事务包装）。`SqliteVectorStore` 实现既有 `IVectorStore` 接口；`SessionPersistence` 公开接口不变、内部换 SQL；`ProjectIndexService` 直连 SQL 单事务提交，删除 `IndexManifest` 类。`SqliteVectorStore` 与 `ProjectIndexService` **共享同一 SqliteStore**，故二者互不嵌套调用（索引服务直接写 SQL，不再经 vector_store_）。规格：`docs/superpowers/specs/2026-08-17-sqlite-storage-design.md`。

**Tech Stack:** C++20、CMake/Ninja、MSYS2 MinGW-w64、SQLite 3.46 amalgamation、sqlite-vec v0.1.6、SQLiteCpp 3.4.0、SQLiteCpp 事务 + nlohmann/json。

---

## ⚠ 仓库状态须知（必读）

当前分支 `integration/session-rag` 的工作区中有**用户暂存的无关 WIP**（文档删除、文件重命名、源码修改等，`git status` 可见）。**禁止** `git add .` / `git add -A` / `git commit` 不带路径。所有提交必须：

```bash
git add <仅本任务涉及的文件路径>
git commit -m "<中文提交信息>" -- <仅本任务涉及的路径列表>
```

`git commit -- <paths>` 只提交列出的路径，不会把用户暂存的其他文件卷进提交。提交后检查 `git log --oneline -1` 确认只含本任务文件。

## 文件地图

**新建：**
- `third_party/sqlite/sqlite3.c`、`sqlite3.h`、`sqlite3ext.h`（下载，不手写）
- `third_party/sqlite-vec/sqlite-vec.c`、`sqlite-vec.h`（克隆后拷贝，不手写）
- `third_party/sqlitecpp/include/SQLiteCpp/*.h`、`third_party/sqlitecpp/src/*.cpp`（克隆后拷贝，不手写）
- `src/storage/SqliteStore.h`、`src/storage/SqliteStore.cpp`
- `src/retrieval/SqliteVectorStore.h`、`src/retrieval/SqliteVectorStore.cpp`
- `tests/test_sqlite_store.cpp`、`tests/test_sqlite_vector_store.cpp`、`tests/test_session_sqlite.cpp`

**删除：**
- `src/retrieval/VectorStore.h`、`src/retrieval/VectorStore.cpp`
- `src/agent/index/IndexManifest.h`、`src/agent/index/IndexManifest.cpp`
- `tests/test_index_manifest.cpp`

**修改：**
- `CMakeLists.txt`（LANGUAGES 加 C、CORE_SOURCES 加 NOVELAGENT_STORAGE、novelagent_sqlite target、链接）
- `cmake/Sources.cmake`（新增 NOVELAGENT_STORAGE、retrieval/agent 列表调整）
- `tests/CMakeLists.txt`（增删测试名，两处列表同步）
- `src/NovelAgentApp.h`、`src/NovelAgentApp.cpp`、`src/AppAssembly.cpp`（组装切换）
- `src/agent/session/SessionPersistence.h`、`src/agent/session/SessionPersistence.cpp`（SQL 化）
- `src/agent/index/ProjectIndexService.h`、`src/agent/index/ProjectIndexService.cpp`（SQL 化）
- `src/retrieval/IVectorStore.h`（顶部注释）
- `tests/test_retrieval.cpp`、`tests/test_agent.cpp`（适配新接口）
- `docs/superpowers/specs/2026-08-17-sqlite-storage-design.md`（补充 embedding_json 列说明）
- `CHANGELOG.md`（实施收尾时增量记录）

---

### Task 1: vendor 依赖 + novelagent_sqlite target

**Files:**
- Create: `third_party/sqlite/`、`third_party/sqlite-vec/`、`third_party/sqlitecpp/`（下载产物）
- Modify: `CMakeLists.txt:16-19`（LANGUAGES）、新增 novelagent_sqlite target
- Test: 现有全量测试保持通过

- [ ] **Step 1: 下载并落位 sqlite3 amalgamation（固定版本 3.46.1）**

```bash
mkdir -p third_party/sqlite third_party/sqlite-vec third_party/sqlitecpp
cd third_party/sqlite
curl -L -o amalgamation.zip https://sqlite.org/2025/sqlite-amalgamation-3460100.zip
tar -xf amalgamation.zip   # Git Bash 的 tar（bsdtar）可直接解 zip；解出的顶层目录是 sqlite-amalgamation-3460100/
mv sqlite-amalgamation-3460100/sqlite3.c sqlite-amalgamation-3460100/sqlite3.h sqlite-amalgamation-3460100/sqlite3ext.h .
rm -rf sqlite-amalgamation-3460100 amalgamation.zip
ls  # 应看到 sqlite3.c sqlite3.h sqlite3ext.h
```

- [ ] **Step 2: 获取 sqlite-vec v0.1.6（dist 目录含 amalgamation）**

```bash
cd /d/C++Code/C++NovelAgent   # 回到项目根
git clone --depth 1 --branch v0.1.6 https://github.com/asg017/sqlite-vec.git /tmp/sqlite-vec-src 2>&1 | tail -2
# 若 tag 不存在则查看可用 tag：git ls-remote --tags https://github.com/asg017/sqlite-vec.git | tail -5
ls /tmp/sqlite-vec-src/dist/   # 应看到 sqlite-vec.c sqlite-vec.h
cp /tmp/sqlite-vec-src/dist/sqlite-vec.c /tmp/sqlite-vec-src/dist/sqlite-vec.h third_party/sqlite-vec/
rm -rf /tmp/sqlite-vec-src
```

- [ ] **Step 3: 获取 SQLiteCpp 3.4.0（拷贝头文件与源文件）**

```bash
git clone --depth 1 --branch 3.4.0 https://github.com/SRombauts/SQLiteCpp.git /tmp/sqlitecpp-src 2>&1 | tail -2
cp -r /tmp/sqlitecpp-src/include/SQLiteCpp third_party/sqlitecpp/include/
mkdir -p third_party/sqlitecpp/src
cp /tmp/sqlitecpp-src/src/*.cpp third_party/sqlitecpp/src/
rm -rf /tmp/sqlitecpp-src
ls third_party/sqlitecpp/src/   # 应看到 Backup.cpp Column.cpp Database.cpp Exception.cpp SQLiteCpp.cpp Statement.cpp Transaction.cpp
```

- [ ] **Step 4: CMakeLists.txt 启用 C 语言并新增 target**

`CMakeLists.txt:16-19` 改动：

```cmake
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES C CXX
)
```

在 `set(COMMON_LIBS ...)` 块之后（约第 63 行）新增：

```cmake
# ── 内置 SQLite（sqlite3 amalgamation + sqlite-vec + SQLiteCpp，全部 vendor）──
# SQLITE_CORE：sqlite-vec 以静态扩展编译进 sqlite3；SQLITE_ENABLE_FTS5：Phase 2 全文索引。
# 本目标不进 PCH：sqlite3.c 为 C 巨型文件，且 PCH 只能用于 C++。
add_library(novelagent_sqlite STATIC
    third_party/sqlite/sqlite3.c
    third_party/sqlite-vec/sqlite-vec.c
    third_party/sqlitecpp/src/SQLiteCpp.cpp
    third_party/sqlitecpp/src/Backup.cpp
    third_party/sqlitecpp/src/Column.cpp
    third_party/sqlitecpp/src/Database.cpp
    third_party/sqlitecpp/src/Exception.cpp
    third_party/sqlitecpp/src/Statement.cpp
    third_party/sqlitecpp/src/Transaction.cpp
)
target_include_directories(novelagent_sqlite PUBLIC
    third_party/sqlite
    third_party/sqlite-vec
    third_party/sqlitecpp/include
)
target_compile_definitions(novelagent_sqlite PUBLIC SQLITE_CORE SQLITE_ENABLE_FTS5)
```

链接接入：`CMakeLists.txt:76` 的 `target_link_libraries(novelagent_core PUBLIC ${COMMON_LIBS})` 改为：

```cmake
target_link_libraries(novelagent_core PUBLIC ${COMMON_LIBS} novelagent_sqlite)
```

同时 `CMakeLists.txt:101` 的 `novelagent_core_prelinked`（测试专用预链接库不复用 OBJECT 库的链接依赖，须手动补）：

```cmake
target_link_libraries(novelagent_core_prelinked PUBLIC ${COMMON_LIBS} novelagent_sqlite)
```

- [ ] **Step 5: 重新配置并构建**

```bash
cmake --preset default
./scripts/verify.sh --build
```

预期：配置成功（编译器检测含 C）；构建成功，_main 等无链接错误。

- [ ] **Step 6: 提交（只提交本任务文件）**

```bash
git add CMakeLists.txt third_party/sqlite third_party/sqlite-vec third_party/sqlitecpp
git commit -m "build: vendor sqlite3+sqlite-vec+SQLiteCpp，新增 novelagent_sqlite 静态库" -- CMakeLists.txt third_party/sqlite third_party/sqlite-vec third_party/sqlitecpp
git log --oneline -1   # 确认只含本任务文件
```

---

### Task 2: SqliteStore + vec0 能力回归测试

**Files:**
- Create: `src/storage/SqliteStore.h`、`src/storage/SqliteStore.cpp`
- Create: `tests/test_sqlite_store.cpp`
- Modify: `cmake/Sources.cmake`（新增 NOVELAGENT_STORAGE）、`CMakeLists.txt`（CORE_SOURCES 加 ${NOVELAGENT_STORAGE}）、`tests/CMakeLists.txt`（两处 CORE_TESTS 列表加 test_sqlite_store）

- [ ] **Step 1: 编写测试（先写测试，编译失败是预期）**

`tests/test_sqlite_store.cpp`：

```cpp
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
    test_kv_roundtrip();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
```

（`SQLite::Statement`/`SQLite::Database` 以 `#include "storage/SqliteStore.h"` 传递引入，测试不直接 include SQLiteCpp。）

- [ ] **Step 2: 接入构建（预期编译失败：缺 SqliteStore）**

`cmake/Sources.cmake` 新增段（放在 NOVELAGENT_PROJECT 之后）：

```cmake
set(NOVELAGENT_STORAGE
    src/storage/SqliteStore.h src/storage/SqliteStore.cpp
)
```

`CMakeLists.txt:53-55` CORE_SOURCES 改为：

```cmake
set(CORE_SOURCES
    ${NOVELAGENT_CONFIG} ${NOVELAGENT_UTILS} ${NOVELAGENT_PROJECT}
    ${NOVELAGENT_STORAGE}
    ${NOVELAGENT_LLM} ${NOVELAGENT_RETRIEVAL} ${NOVELAGENT_AGENT}
    ${NOVELAGENT_SKILL}
)
```

`tests/CMakeLists.txt:12-17` 的 CORE_TESTS foreach 列表与 `:42-47` 的 CORE_TESTS（ENVIRONMENT_MODIFICATION）列表**两处都**加入 `test_sqlite_store`。

```bash
cmake --preset default
./scripts/verify.sh "test_sqlite_store"
```

预期：编译失败，错误为找不到 `storage/SqliteStore.h`（Test 先行的 red 状态）。

- [ ] **Step 3: 实现 SqliteStore**

`src/storage/SqliteStore.h`：

```cpp
#pragma once

// SqliteStore — SQLite 单库入口（产品代码内唯一的 SQLite 连接）。
//
// 线程模型：全库一把互斥锁。withLock/inTransaction 自行加锁，回调内
// **禁止**再调用本类任何加锁方法（包括二者本身，不可嵌套）。
// exec/getKV/setKV/ensureVectorTable/db 均为"锁内使用"的低层方法，
// 只能在 withLock/inTransaction 回调内调用（文档约束，编译期不强制）。
// open/close 是唯一可在锁外调用的方法（应用生命周期保证不与锁内回调并发）。
//
// 异常策略：inTransaction 回调抛异常 → 自动 ROLLBACK 并重抛；其他低层方法
// 不捕获 SQLiteCpp 异常（由调用方按语义处理）；open 的建表失败走损坏自愈
// （改名 .corrupt-<时间戳> → 重建空库），保证应用可用性。

#include <memory>
#include <mutex>
#include <string>

namespace SQLite { class Database; }

namespace storage {

class SqliteStore {
public:
    SqliteStore() = default;
    ~SqliteStore() { close(); }

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    // 打开/创建 db_path；已打开则忽略。首次打开执行建表迁移与 PRAGMA。
    // 建表失败视为库损坏：改名 novel.db.corrupt-<时间戳> 后重建空库。
    void open(const std::string& db_path);
    // 关闭连接（WAL 由 SQLite 在最后连接关闭时自动 checkpoint）；未打开时 no-op。
    void close();
    bool isOpen() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }

    // 锁内执行读回调；回调仅可调用本类锁内方法。
    template <typename F>
    auto withLock(F&& f) -> decltype(f(*this)) {
        std::lock_guard<std::mutex> lock(mutex_);
        return f(*this);
    }

    // 锁内事务：回调正常返回 → COMMIT；抛异常 → ROLLBACK 并重抛。
    template <typename F>
    auto inTransaction(F&& f) -> decltype(f(*this)) {
        std::lock_guard<std::mutex> lock(mutex_);
        SQLite::Database& db = *db_;
        db.exec("BEGIN IMMEDIATE");
        try {
            auto r = f(*this);
            db.exec("COMMIT");
            return r;
        } catch (...) {
            db.exec("ROLLBACK");
            throw;
        }
    }

    // ── 锁内低层操作 ──

    SQLite::Database& db() { return *db_; }

    // 执行无参 SQL（建表/PRAGMA 等）。
    void exec(const std::string& sql);

    // kv_store 存取（模型指纹等）；key 不存在返回空串。
    std::string getKV(const std::string& key);
    void setKV(const std::string& key, const std::string& value);

    // 确保 vec_chunks 存在（维度 dimension）；维度与库中不同时 DROP 重建。
    // 注意：DROP 会清空全部向量，调用者须在指纹失配等语义下使用。
    void ensureVectorTable(int dimension);
    int vectorDimension() const { return vector_dimension_; }

private:
    // 建全部业务表与 PRAGMA（sessions/messages/message_history/index_sources/
    // index_chunks/kv_store；vec_chunks 由 ensureVectorTable 按维度创建）。
    void ensureSchema();
    // 损坏自愈：改名 + 重建空库；再次失败则保持未打开。
    void removeCorruptAndReopen();

    std::unique_ptr<SQLite::Database> db_;
    mutable std::mutex mutex_;
    std::string path_;
    int vector_dimension_ = 0;  // 内存缓存当前向量维度；打开时从库内推理
};

} // namespace storage
```

`src/storage/SqliteStore.cpp`：

```cpp
// SqliteStore 实现 — 建表迁移、WAL、事务包装与损坏自愈。

#include "storage/SqliteStore.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <system_error>

namespace storage {

namespace {

// 当前时间戳 "2026-08-17T03:15:00Z"（文件后缀安全）。
std::string nowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

} // namespace

void SqliteStore::open(const std::string& db_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) return;

    path_ = db_path;
    const std::string dir = utils::file::dirName(db_path);
    if (!dir.empty() && !utils::file::exists(dir)) {
        utils::file::createDirs(dir);
    }

    try {
        db_ = std::make_unique<SQLite::Database>(
            db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        ensureSchema();
    } catch (const std::exception& e) {
        spdlog::error("[SqliteStore] 打开库失败（尝试损坏自愈）: {} - {}", db_path, e.what());
        db_.reset();
        removeCorruptAndReopen();
    }
    spdlog::info("[SqliteStore] 已打开数据库: {} ({} 张业务表)", db_path, 6);
}

void SqliteStore::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;
    // 显式 checkpoint 不是必须的：SQLite 在最后一个连接关闭时自动完成
    // WAL checkpoint；close 仅释放连接。
    db_.reset();
    vector_dimension_ = 0;
    spdlog::debug("[SqliteStore] 已关闭数据库");
}

void SqliteStore::exec(const std::string& sql)
{
    db_->exec(sql);
}

std::string SqliteStore::getKV(const std::string& key)
{
    SQLite::Statement stmt(*db_, "SELECT value FROM kv_store WHERE key = ?");
    stmt.bind(1, key);
    if (!stmt.executeStep()) return {};
    return stmt.getColumn(0).getString();
}

void SqliteStore::setKV(const std::string& key, const std::string& value)
{
    SQLite::Statement stmt(*db_,
        "INSERT INTO kv_store (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    stmt.bind(1, key);
    stmt.bind(2, value);
    stmt.exec();
}

void SqliteStore::ensureVectorTable(int dimension)
{
    if (!db_ || dimension <= 0) return;
    if (vector_dimension_ == dimension) return;

    // DROP 旧表（维度不同或首次建表）：虚拟表不支持 ALTER 改维度，
    // 维度变更语义即"整库失效"（调用方已在指纹层清空清单）。
    db_->exec("DROP TABLE IF EXISTS vec_chunks");
    // vec0 附加列：chunk_id/metadata/embedding_json 随行存储，可查询返回；
    // embedding_json 保存原始向量（JSON），避免依赖 vec0 内部存储格式。
    const std::string ddl =
        "CREATE VIRTUAL TABLE vec_chunks USING vec0("
        "  chunk_id TEXT,"
        "  metadata TEXT,"
        "  embedding_json TEXT,"
        "  embedding float[" + std::to_string(dimension) + "],"
        "  distance_metric='cosine'"
        ")";
    db_->exec(ddl);
    vector_dimension_ = dimension;
    spdlog::info("[SqliteStore] vec_chunks 已创建 (维度 {})", dimension);
}

void SqliteStore::ensureSchema()
{
    db_->exec("PRAGMA journal_mode=WAL");
    db_->exec("PRAGMA foreign_keys=ON");

    // 会话（archived=1 为归档态：数据保留、列表不可见）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS sessions ("
        " id TEXT PRIMARY KEY,"
        " title TEXT NOT NULL DEFAULT '',"
        " created_at TEXT NOT NULL,"
        " updated_at TEXT NOT NULL,"
        " archived INTEGER NOT NULL DEFAULT 0"
        ")");

    // 快照层：save() 事务内 DELETE + 重插（对应原 <id>.json）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        " session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
        " seq INTEGER NOT NULL,"
        " role TEXT NOT NULL,"
        " content TEXT NOT NULL DEFAULT '',"
        " tool_calls TEXT,"
        " tool_call_id TEXT,"
        " reasoning_content TEXT,"
        " preserved INTEGER NOT NULL DEFAULT 0,"
        " is_control INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY (session_id, seq)"
        ")");

    // 完整历史层：append-only（对应原 <id>.history）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS message_history ("
        " session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
        " seq INTEGER NOT NULL,"
        " role TEXT NOT NULL,"
        " content TEXT NOT NULL DEFAULT '',"
        " tool_calls TEXT,"
        " tool_call_id TEXT,"
        " reasoning_content TEXT,"
        " preserved INTEGER NOT NULL DEFAULT 0,"
        " is_control INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY (session_id, seq)"
        ")");

    // 索引清单（对应原 index_manifest.json）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS index_sources ("
        " source_key TEXT PRIMARY KEY,"
        " content_hash TEXT NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ")");
    db_->exec(
        "CREATE TABLE IF NOT EXISTS index_chunks ("
        " source_key TEXT NOT NULL REFERENCES index_sources(source_key) ON DELETE CASCADE,"
        " chunk_id TEXT NOT NULL,"
        " PRIMARY KEY (source_key, chunk_id)"
        ")");

    // 单行 KV（模型指纹、schema 版本等）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS kv_store ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL"
        ")");
}

void SqliteStore::removeCorruptAndReopen()
{
    try {
        const std::string backup = path_ + ".corrupt-" + nowTimestamp();
        std::error_code ec;
        std::filesystem::rename(path_, backup, ec);
        if (ec) {
            spdlog::error("[SqliteStore] 损坏库改名失败: {} - {}", path_, ec.message());
            return;  // 保持未打开
        }
        spdlog::warn("[SqliteStore] 损坏库已改名备份: {}", backup);
    } catch (const std::exception& e) {
        spdlog::error("[SqliteStore] 损坏库改名失败: {}", e.what());
        return;
    }

    try {
        db_ = std::make_unique<SQLite::Database>(
            path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        ensureSchema();
        spdlog::warn("[SqliteStore] 已重建空库: {}", path_);
    } catch (const std::exception& e) {
        db_.reset();
        spdlog::error("[SqliteStore] 重建空库失败，库保持关闭: {}", e.what());
    }
}

} // namespace storage
```

- [ ] **Step 4: 构建并运行测试**

```bash
cmake --preset default        # Sources.cmake/CMakeLists 变更后需要
./scripts/verify.sh "test_sqlite_store"
```

预期：9 项全部 PASSED，退出码 0。（首跑若 vec0 附加列/`k` 参数行为与预期不符，以实测为基准修正测试或用法，修正后必须全绿再提交。）

- [ ] **Step 5: 提交**

```bash
git add src/storage cmake/Sources.cmake CMakeLists.txt tests/test_sqlite_store.cpp tests/CMakeLists.txt
git commit -m "feat(storage): 新增 SqliteStore（建表/WAL/事务/损坏自愈）+ vec0 能力回归测试" -- src/storage cmake/Sources.cmake CMakeLists.txt tests/test_sqlite_store.cpp tests/CMakeLists.txt
git log --oneline -1
```

---

### Task 3: SqliteVectorStore + 全语义测试

**Files:**
- Create: `src/retrieval/SqliteVectorStore.h`、`src/retrieval/SqliteVectorStore.cpp`
- Create: `tests/test_sqlite_vector_store.cpp`
- Modify: `docs/superpowers/specs/2026-08-17-sqlite-storage-design.md`（补充 embedding_json 列）、`tests/CMakeLists.txt`（两处 CORE_TESTS 加 test_sqlite_vector_store）、`cmake/Sources.cmake`（NOVELAGENT_RETRIEVAL）

- [ ] **Step 1: 规格补充一行（实现细节决策）**

`docs/superpowers/specs/2026-08-17-sqlite-storage-design.md` 的 vec_chunks DDL 块后追加：

```markdown
> 实现补充：vec_chunks 增加附加列 `embedding_json TEXT`（原始向量 JSON）。理由：vec0 内部存储格式属实现细节，不做依赖假设；`get()` 需按原样还原向量，借此列往返。
```

- [ ] **Step 2: 编写测试（先写，编译失败是预期）**

`tests/test_sqlite_vector_store.cpp`（与旧 test_retrieval 的 VectorStore 用例语义对齐；`#include "retrieval/SqliteVectorStore.h"` + `#include "storage/SqliteStore.h"`）：

```cpp
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
```

- [ ] **Step 3: 接入构建（预期编译失败）**

`cmake/Sources.cmake` 的 NOVELAGENT_RETRIEVAL 段落改为：

```cmake
set(NOVELAGENT_RETRIEVAL
    src/retrieval/SqliteVectorStore.h src/retrieval/SqliteVectorStore.cpp
    src/retrieval/IVectorStore.h
    src/retrieval/EmbeddingGenerator.h src/retrieval/EmbeddingGenerator.cpp
    src/retrieval/IEmbeddingGenerator.h
    src/retrieval/NovelChunker.h src/retrieval/NovelChunker.cpp
)
```

`tests/CMakeLists.txt` 两处 CORE_TESTS 列表加入 `test_sqlite_vector_store`。

```bash
cmake --preset default
./scripts/verify.sh "test_sqlite_vector_store"
```

预期：编译失败，找不到 `retrieval/SqliteVectorStore.h`。

- [ ] **Step 4: 实现 SqliteVectorStore**

`src/retrieval/SqliteVectorStore.h`：

```cpp
#pragma once

// SqliteVectorStore — SQLite（sqlite-vec vec0）后端的向量存储。
//
// 实现 IVectorStore 抽象接口（替换旧 JSON 文件 VectorStore）。与
// ProjectIndexService 共享同一 SqliteStore——索引服务直连 SQL 写库，
// 本类只服务检索调用方（工具等），二者互不嵌套，避免重复加锁。
//
// 相似度契约：搜索返回 similarity ∈ [0,1] 降序（与旧实现一致）。
// sqlite-vec 返回 cosine distance ∈ [0,2]，换算 similarity = 1 - d/2。

#include "retrieval/IVectorStore.h"

#include <optional>
#include <string>
#include <vector>

namespace storage { class SqliteStore; }

namespace retrieval {

class SqliteVectorStore : public IVectorStore {
public:
    // @param store SQLite 单库；非拥有引用，调用方保证存活期覆盖本对象。
    explicit SqliteVectorStore(storage::SqliteStore& store) : store_(store) {}

    void insert(const std::string& id,
                const EmbeddingVector& embedding,
                const nlohmann::json& metadata) override;
    void insertBatch(const std::vector<VectorEntry>& entries) override;
    bool remove(const std::string& id) override;
    void update(const std::string& id, const EmbeddingVector& embedding) override;

    std::vector<SearchResult> search(const EmbeddingVector& query_embedding,
                                     int top_k = 10) const override;

    int count() const override;
    bool contains(const std::string& id) const override;

    // 事务即持久化：无需显式落盘。为兼容 IVectorStore 契约保留为空实现。
    void flush() override {}

    // 与旧 VectorStore 兼容的查询：按 id 返回条目副本；未命中返回 nullopt。
    std::optional<VectorEntry> get(const std::string& id) const;

private:
    storage::SqliteStore& store_;
};

} // namespace retrieval
```

`src/retrieval/SqliteVectorStore.cpp`：

```cpp
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
```

- [ ] **Step 5: 构建并运行测试**

```bash
cmake --preset default
./scripts/verify.sh "test_sqlite_vector_store|test_sqlite_store"
```

预期：两文件测试全绿。

- [ ] **Step 6: 提交**

```bash
git add src/retrieval/SqliteVectorStore.h src/retrieval/SqliteVectorStore.cpp tests/test_sqlite_vector_store.cpp cmake/Sources.cmake tests/CMakeLists.txt docs/superpowers/specs/2026-08-17-sqlite-storage-design.md
git commit -m "feat(retrieval): 新增 SqliteVectorStore（vec0 后端，语义对齐旧实现）" -- src/retrieval/SqliteVectorStore.h src/retrieval/SqliteVectorStore.cpp tests/test_sqlite_vector_store.cpp cmake/Sources.cmake tests/CMakeLists.txt docs/superpowers/specs/2026-08-17-sqlite-storage-design.md
git log --oneline -1
```

---

### Task 4: SessionPersistence SQL 化 + 测试

**Files:**
- Rewrite: `src/agent/session/SessionPersistence.h`、`src/agent/session/SessionPersistence.cpp`
- Create: `tests/test_session_sqlite.cpp`
- Modify: `tests/test_agent.cpp`（5 处构造点）、`tests/CMakeLists.txt`（两处 CORE_TESTS 加 test_session_sqlite）

- [ ] **Step 1: 重写头文件（公开接口不变）**

`src/agent/session/SessionPersistence.h` 全文替换为：

```cpp
#pragma once

// 会话持久化 — 多会话的保存/加载/切换/删除（SQLite 实现）。
//
// 持久化布局（novel.db，见 SqliteStore::ensureSchema）：
//   - sessions 表          → 会话元信息（archived=1 为归档态：数据保留、列表不可见）
//   - messages 表          → 快照层（save() 事务内 DELETE + 重插，对应原 <id>.json）
//   - message_history 表   → 完整历史层（append-only，对应原 <id>.history，
//                            appendHistory 事务内从 MAX(seq)+1 起连续编号）
//
// 设计要点（与原文件版保持一致）：
//   - system prompt 不持久化——每次启动由 NovelAgentApp 重新组装。
//   - preserved（/pin）标记随消息一同持久化，跨重启保留。
//   - 压缩摘要以普通消息形式存在于快照层，随会话自然恢复。
//   - 会话标题在首次保存时从首条 user 消息自动提取（UTF-8 安全截断）。
//
// 线程安全：全部操作经 SqliteStore 的全库锁串行化（原 index_mutex_ 删除）。

#include "project/FileStorageBackend.h"

#include <string>
#include <vector>

namespace storage { class SqliteStore; }
namespace llm {
class IMemory;
class Memory;
struct Message;
} // namespace llm

namespace agent {

// 会话元信息（sessions 表一行）。
struct SessionInfo {
    std::string id;
    std::string title;       // 空 = 尚无用户消息（前端显示"新会话"）
    std::string created_at;  // ISO 8601 UTC
    std::string updated_at;  // ISO 8601 UTC
};

// 会话持久化管理器（SQLite 实现，公开接口与原文件版一致）。
class SessionPersistence {
public:
    // @param sqlite  SQLite 单库（非拥有引用；调用方保证存活期覆盖本对象）。
    // @param storage 文件存储后端（仅用于 nowTimestamp 时间戳）。
    SessionPersistence(storage::SqliteStore& sqlite, FileStorageBackend& storage)
        : sqlite_(sqlite), storage_(storage) {}

    // ── 按显式 session_id 读写 ──

    // 保存完整对话历史（快照层全量覆盖，事务内 DELETE + 重插）；
    // 同时刷新 updated_at，并在标题为空时从首条 user 消息自动提取标题。
    void save(const std::string& session_id, const llm::IMemory& memory);

    // 加载指定会话快照（不含 system prompt）；不存在返回空 Memory。
    llm::Memory load(const std::string& session_id);

    // 追加被压缩消息到完整历史层（append-only，事务内续号）。
    void appendHistory(const std::string& session_id,
                       const std::vector<llm::Message>& messages);

    // 读取完整历史层全部消息（按 seq 升序）。
    std::vector<llm::Message> loadHistory(const std::string& session_id);

    // ── 会话管理 ──

    // 会话列表（不含归档，按 updated_at 降序，最近使用在前）。
    std::vector<SessionInfo> listSessions();

    // 新建空会话，返回新会话 id（s-<时间戳> 格式）。
    std::string createSession();

    // 删除指定会话：置 sessions.archived=1（数据保留、列表不可见）。
    bool deleteSession(const std::string& id);

private:
    // 由时间戳生成会话 id；sessions 表（含归档）已存在时追加序号。
    // 须在 sqlite_ 锁内调用。
    std::string makeSessionId(const std::string& timestamp) const;

    storage::SqliteStore& sqlite_;
    FileStorageBackend& storage_;
};

} // namespace agent
```

- [ ] **Step 2: 重写实现（SQL 版）**

`src/agent/session/SessionPersistence.cpp` 全文替换为：

```cpp
// SessionPersistence 实现 — SQLite 表读写（快照层 + 完整历史层）。

#include "agent/session/SessionPersistence.h"

#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "storage/SqliteStore.h"

#include <SQLiteCpp/Statement.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace agent {

namespace {

// ── 消息 ↔ 行字段 ──

// system 消息一律不落盘（system prompt 由启动时重新组装）。
bool isSystem(const llm::Message& m) { return m.role == llm::MessageRole::System; }

std::string toolCallsToJson(const llm::Message& m)
{
    if (m.tool_calls.empty()) return {};
    nlohmann::json j = m.tool_calls;
    return j.dump();
}

std::vector<llm::ToolCall> parseToolCalls(const std::string& s)
{
    if (s.empty()) return {};
    try {
        return nlohmann::json::parse(s).get<std::vector<llm::ToolCall>>();
    } catch (...) {
        return {};
    }
}

// 绑定一条消息到 8 个业务列（seq, role, content, tool_calls, tool_call_id,
// reasoning_content, preserved, is_control）。
void bindMessageColumns(SQLite::Statement& stmt, const llm::Message& m, int seq)
{
    stmt.bind(1, seq);
    stmt.bind(2, llm::roleToString(m.role));
    stmt.bind(3, m.content);
    stmt.bind(4, toolCallsToJson(m));
    stmt.bind(5, m.tool_call_id);
    stmt.bind(6, m.reasoning_content);
    stmt.bind(7, m.preserved ? 1 : 0);
    stmt.bind(8, m.is_control ? 1 : 0);
}

// 从行列还原 Message（列序与 bindMessageColumns 一致）。
llm::Message rowToMessage(SQLite::Statement& stmt)
{
    llm::Message m;
    m.role = llm::roleFromString(stmt.getColumn(1).getString());
    m.content = stmt.getColumn(2).getString();
    m.tool_calls = parseToolCalls(stmt.getColumn(3).getString());
    m.tool_call_id = stmt.getColumn(4).getString();
    m.reasoning_content = stmt.getColumn(5).getString();
    m.preserved = stmt.getColumn(6).getInt() != 0;
    m.is_control = stmt.getColumn(7).getInt() != 0;
    return m;
}

// UTF-8 安全截断：最多保留 max_bytes 字节，退到字符边界，截断时追加省略号。
std::string utf8Truncate(const std::string& s, size_t max_bytes)
{
    if (s.size() <= max_bytes) return s;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end) + "…";
}

// 从 messages 提取首条 user 消息的首行作为会话标题；无 user 消息返回空。
std::string deriveTitle(const std::vector<llm::Message>& messages)
{
    for (const auto& m : messages) {
        if (m.role != llm::MessageRole::User) continue;
        std::string content = m.content;
        if (auto nl = content.find('\n'); nl != std::string::npos)
            content = content.substr(0, nl);
        return utf8Truncate(content, 30);
    }
    return {};
}

} // namespace

// ── 按显式 session_id 读写 ──

void SessionPersistence::save(const std::string& session_id, const llm::IMemory& memory)
{
    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        SQLite::Database& db = s.db();
        const std::string ts = storage_.nowTimestamp();
        const auto& msgs = memory.messages();

        // 1) 会话登记：upsert；已存在时仅刷新 updated_at 与空标题
        {
            SQLite::Statement upsert(db,
                "INSERT INTO sessions (id, title, created_at, updated_at) VALUES (?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET "
                " updated_at = excluded.updated_at,"
                " title = CASE WHEN sessions.title = '' THEN excluded.title ELSE sessions.title END");
            upsert.bind(1, session_id);
            upsert.bind(2, deriveTitle(msgs));
            upsert.bind(3, ts);
            upsert.bind(4, ts);
            upsert.exec();
        }
        // 2) 快照层：全量覆盖（DELETE + 重插）
        {
            SQLite::Statement del(db, "DELETE FROM messages WHERE session_id = ?");
            del.bind(1, session_id);
            del.exec();
        }
        SQLite::Statement ins(db,
            "INSERT INTO messages (session_id, seq, role, content, tool_calls,"
            " tool_call_id, reasoning_content, preserved, is_control)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        int seq = 1;
        for (const auto& m : msgs) {
            if (isSystem(m)) continue;
            ins.bind(1, session_id);
            bindMessageColumns(ins, m, seq++);
            ins.exec();
        }
    });
    spdlog::info("[SessionPersistence] 会话 {} 已保存 (快照更新)", session_id);
}

llm::Memory SessionPersistence::load(const std::string& session_id)
{
    llm::Memory mem;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT seq, role, content, tool_calls, tool_call_id, reasoning_content,"
            " preserved, is_control FROM messages WHERE session_id = ? ORDER BY seq");
        stmt.bind(1, session_id);
        while (stmt.executeStep()) {
            llm::Message m = rowToMessage(stmt);
            if (m.role == llm::MessageRole::System) continue;  // 防御
            mem.inject(std::move(m));
        }
    });
    spdlog::info("[SessionPersistence] 会话 {} 已加载 ({} 条消息)", session_id, mem.size());
    return mem;
}

void SessionPersistence::appendHistory(const std::string& session_id,
                                       const std::vector<llm::Message>& messages)
{
    std::vector<const llm::Message*> targets;
    for (const auto& m : messages) {
        if (!isSystem(m)) targets.push_back(&m);
    }
    if (targets.empty()) return;

    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        SQLite::Database& db = s.db();
        // 续号：从当前最大 seq 之后连续编号
        int seq = 1;
        {
            SQLite::Statement maxq(db,
                "SELECT COALESCE(MAX(seq), 0) + 1 FROM message_history WHERE session_id = ?");
            maxq.bind(1, session_id);
            if (maxq.executeStep()) seq = maxq.getColumn(0).getInt();
        }
        SQLite::Statement ins(db,
            "INSERT INTO message_history (session_id, seq, role, content, tool_calls,"
            " tool_call_id, reasoning_content, preserved, is_control)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        for (const llm::Message* m : targets) {
            ins.bind(1, session_id);
            bindMessageColumns(ins, *m, seq++);
            ins.exec();
        }
    });
    spdlog::info("[SessionPersistence] 会话 {} 完整历史追加 {} 条消息",
                 session_id, targets.size());
}

std::vector<llm::Message> SessionPersistence::loadHistory(const std::string& session_id)
{
    std::vector<llm::Message> result;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT seq, role, content, tool_calls, tool_call_id, reasoning_content,"
            " preserved, is_control FROM message_history WHERE session_id = ? ORDER BY seq");
        stmt.bind(1, session_id);
        while (stmt.executeStep()) {
            llm::Message m = rowToMessage(stmt);
            if (m.role == llm::MessageRole::System) continue;
            result.push_back(std::move(m));
        }
    });
    return result;
}

// ── 会话管理 ──

std::vector<SessionInfo> SessionPersistence::listSessions()
{
    std::vector<SessionInfo> result;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT id, title, created_at, updated_at FROM sessions WHERE archived = 0 "
            "ORDER BY updated_at DESC");
        while (stmt.executeStep()) {
            SessionInfo info;
            info.id = stmt.getColumn(0).getString();
            info.title = stmt.getColumn(1).getString();
            info.created_at = stmt.getColumn(2).getString();
            info.updated_at = stmt.getColumn(3).getString();
            result.push_back(std::move(info));
        }
    });
    return result;
}

std::string SessionPersistence::createSession()
{
    return sqlite_.inTransaction([&](storage::SqliteStore& s) -> std::string {
        const std::string ts = storage_.nowTimestamp();
        const std::string id = makeSessionId(ts);
        SQLite::Statement ins(s.db(),
            "INSERT INTO sessions (id, title, created_at, updated_at, archived)"
            " VALUES (?, '', ?, ?, 0)");
        ins.bind(1, id);
        ins.bind(2, ts);
        ins.bind(3, ts);
        ins.exec();
        spdlog::info("[SessionPersistence] 新会话已创建: {}", id);
        return id;
    });
}

bool SessionPersistence::deleteSession(const std::string& id)
{
    return sqlite_.inTransaction([&](storage::SqliteStore& s) -> bool {
        SQLite::Database& db = s.db();
        // 先确认存在（未归档）再置归档，避免依赖 exec() 的变更计数返回值
        {
            SQLite::Statement q(db, "SELECT 1 FROM sessions WHERE id = ? AND archived = 0");
            q.bind(1, id);
            if (!q.executeStep()) return false;
        }
        SQLite::Statement upd(db, "UPDATE sessions SET archived = 1 WHERE id = ?");
        upd.bind(1, id);
        upd.exec();
        spdlog::info("[SessionPersistence] 会话 {} 已删除（归档，数据保留）", id);
        return true;
    });
}

std::string SessionPersistence::makeSessionId(const std::string& timestamp) const
{
    // "2026-07-27T03:15:00Z" → "s-20260727T031500Z"（沿用原格式）
    std::string compact;
    for (char c : timestamp) {
        if (c != ':' && c != '-') compact += c;
    }
    const std::string base = "s-" + compact;
    std::string candidate = base;
    int n = 2;
    // 查重含归档会话：已删除 id 不同秒复用，保持 id 全局唯一
    auto taken = [&](const std::string& id) {
        SQLite::Statement stmt(sqlite_.db(),
            "SELECT 1 FROM sessions WHERE id = ?");
        stmt.bind(1, id);
        return stmt.executeStep();
    };
    while (taken(candidate))
        candidate = base + "-" + std::to_string(n++);
    return candidate;
}

} // namespace agent
```

- [ ] **Step 3: 编写测试（先写，预期编译失败）**

`tests/test_session_sqlite.cpp`：

```cpp
// SessionPersistence SQLite 实现回归测试（对齐原文件版语义）。

#include "agent/session/SessionPersistence.h"
#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "project/FileStorageBackend.h"
#include "storage/SqliteStore.h"
#include "utils/FileUtils.h"

#include <filesystem>
#include <iostream>
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
}

void test_save_load_roundtrip() {
    TEST("save/load — 快照往返（含 tool_calls/preserved/is_control/顺序）");
    const std::string db_path = tmpPath("tmp_test_sess_rnd.db");
    const std::string proj_path = tmpPath("tmp_test_sess_rnd_proj");
    cleanup(db_path);
    std::filesystem::create_directories(proj_path);

    storage::SqliteStore sqlite;
    sqlite.open(db_path);
    FileStorageBackend storage(proj_path);
    agent::SessionPersistence persistence(sqlite, storage);

    llm::Memory mem;
    mem.setSystemPrompt("你是助手。");
    mem.addUser("你好");
    llm::Message asst = llm::Message::assistant("你好！");
    asst.tool_calls = {{"call-1", "function", "save_memory", "{\"text\":\"x\"}"},
                       {"call-2", "function", "search_memory", "{\"q\":\"y\"}"}};
    mem.inject(asst);
    llm::Message control = llm::Message::assistant("");
    control.is_control = true;
    mem.inject(control);
    llm::Message pinned = llm::Message::user("别忘了我叫小明");
    pinned.preserved = true;
    mem.inject(pinned);

    persistence.save("s-20260817T000000Z", mem);

    auto loaded = persistence.load("s-20260817T000000Z");
    CHECK(loaded.size() == 4);  // system 不落盘
    CHECK(loaded.messages()[0].role == llm::MessageRole::User);
    CHECK(loaded.messages()[0].content == "你好");
    CHECK(loaded.messages()[1].tool_calls.size() == 2);
    CHECK(loaded.messages()[1].tool_calls[1].function_name == "search_memory");
    CHECK(loaded.messages()[2].is_control);
    CHECK(loaded.messages()[3].preserved);

    sqlite.close();
    std::filesystem::remove_all(proj_path);
    cleanup(db_path);
    PASS();
}

void test_title_derivation() {
    TEST("保存 — 首条 user 首行截断为标题");
    const std::string db_path = tmpPath("tmp_test_sess_title.db");
    const std::string proj_path = tmpPath("tmp_test_sess_title_proj");
    cleanup(db_path);
    std::filesystem::create_directories(proj_path);

    storage::SqliteStore sqlite;
    sqlite.open(db_path);
    FileStorageBackend storage(proj_path);
    agent::SessionPersistence persistence(sqlite, storage);

    std::string long_line(40, '长');
    llm::Memory mem;
    mem.addUser(long_line + "\n第二行");
    persistence.save("s-1", mem);

    auto sessions = persistence.listSessions();
    CHECK(sessions.size() == 1);
    CHECK(sessions[0].id == "s-1");
    CHECK(!sessions[0].title.empty());
    CHECK(sessions[0].title.back() == '…');  // 截断标记

    sqlite.close();
    std::filesystem::remove_all(proj_path);
    cleanup(db_path);
    PASS();
}

void test_append_load_history() {
    TEST("appendHistory/loadHistory — append-only 与 seq 续号");
    const std::string db_path = tmpPath("tmp_test_sess_hist.db");
    const std::string proj_path = tmpPath("tmp_test_sess_hist_proj");
    cleanup(db_path);
    std::filesystem::create_directories(proj_path);

    storage::SqliteStore sqlite;
    sqlite.open(db_path);
    FileStorageBackend storage(proj_path);
    agent::SessionPersistence persistence(sqlite, storage);

    CHECK(persistence.loadHistory("s-9").empty());
    persistence.appendHistory("s-9", {llm::Message::user("第一批1"),
                                      llm::Message::assistant("第一批2")});
    persistence.appendHistory("s-9", {llm::Message::user("第二批")});

    auto h = persistence.loadHistory("s-9");
    CHECK(h.size() == 3);
    CHECK(h[0].content == "第一批1");
    CHECK(h[1].content == "第一批2");
    CHECK(h[2].content == "第二批");  // 续号不覆盖

    sqlite.close();
    std::filesystem::remove_all(proj_path);
    cleanup(db_path);
    PASS();
}

void test_create_list_delete() {
    TEST("createSession/listSessions/deleteSession — 归档语义");
    const std::string db_path = tmpPath("tmp_test_sess_mgmt.db");
    const std::string proj_path = tmpPath("tmp_test_sess_mgmt_proj");
    cleanup(db_path);
    std::filesystem::create_directories(proj_path);

    storage::SqliteStore sqlite;
    sqlite.open(db_path);
    FileStorageBackend storage(proj_path);
    agent::SessionPersistence persistence(sqlite, storage);

    const std::string id1 = persistence.createSession();
    const std::string id2 = persistence.createSession();
    CHECK(id1 != id2);
    CHECK(!id1.empty() && id1.rfind("s-", 0) == 0);

    // 保存消息后列表含 2 个
    llm::Memory mem;
    mem.addUser("内容");
    persistence.save(id1, mem);
    CHECK(persistence.listSessions().size() == 2);

    // 删除 → 列表不可见，但数据保留（重建列表前 save 后 load 仍可恢复）
    CHECK(persistence.deleteSession(id1));
    CHECK(!persistence.deleteSession("s-nonexistent"));
    auto sessions = persistence.listSessions();
    CHECK(sessions.size() == 1);
    CHECK(sessions[0].id == id2);

    // 归档态数据仍在库中：同 id 再次 createSession 会避开它（序号后缀）
    (void)persistence.createSession();
    CHECK(persistence.listSessions().size() == 2);

    sqlite.close();
    std::filesystem::remove_all(proj_path);
    cleanup(db_path);
    PASS();
}

void test_persistence_across_reopen() {
    TEST("跨重启 — 重开库后会话可恢复");
    const std::string db_path = tmpPath("tmp_test_sess_reopen.db");
    const std::string proj_path = tmpPath("tmp_test_sess_reopen_proj");
    cleanup(db_path);
    std::filesystem::create_directories(proj_path);

    {
        storage::SqliteStore sqlite;
        sqlite.open(db_path);
        FileStorageBackend storage(proj_path);
        agent::SessionPersistence persistence(sqlite, storage);
        llm::Memory mem;
        mem.addUser("持久化验证");
        persistence.save("s-77", mem);
        sqlite.close();
    }
    {
        storage::SqliteStore sqlite;
        sqlite.open(db_path);
        FileStorageBackend storage(proj_path);
        agent::SessionPersistence persistence(sqlite, storage);
        auto sessions = persistence.listSessions();
        CHECK(sessions.size() == 1);
        auto loaded = persistence.load("s-77");
        CHECK(loaded.size() == 1);
        CHECK(loaded.messages()[0].content == "持久化验证");
        sqlite.close();
    }
    std::filesystem::remove_all(proj_path);
    cleanup(db_path);
    PASS();
}

int main() {
    std::cout << "=== test_session_sqlite ===\n\n";
    test_save_load_roundtrip();
    test_title_derivation();
    test_append_load_history();
    test_create_list_delete();
    test_persistence_across_reopen();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
```

- [ ] **Step 4: 接入构建并更新 test_agent.cpp 的 5 处构造点**

`tests/CMakeLists.txt` 两处 CORE_TESTS 列表加入 `test_session_sqlite`。

`tests/test_agent.cpp` 现有构造模式（5 处，行号约 370-371、433-434、616-617、729-730、837-838）：

```cpp
    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);
```

统一替换为：

```cpp
    FileStorageBackend storage(tmp);
    storage::SqliteStore sqlite;
    sqlite.open((std::filesystem::temp_directory_path() / "test_agent.db").string());
    agent::SessionPersistence persistence(sqlite, storage);
```

并在对应测试函数末尾（`PASS()` 前）追加 `sqlite.close();`。需要新增 include：`#include "storage/SqliteStore.h"`。测试文件顶部若已有 `#include <filesystem>` 则直接可用；没有则补。**注意保持每处局部变量名不冲突**（5 个测试函数作用域相互独立，`sqlite` 同名安全）。

```bash
cmake --preset default
./scripts/verify.sh "test_session_sqlite|test_agent"
```

预期：test_session_sqlite 全绿；test_agent 与会话持久化相关的用例（save/load/loadHistory/multi-session）全绿。

- [ ] **Step 5: 提交**

```bash
git add src/agent/session/SessionPersistence.h src/agent/session/SessionPersistence.cpp tests/test_session_sqlite.cpp tests/test_agent.cpp tests/CMakeLists.txt
git commit -m "refactor(session): SessionPersistence 改用 SQLite 表实现（公开接口不变，删除会话置归档）" -- src/agent/session/SessionPersistence.h src/agent/session/SessionPersistence.cpp tests/test_session_sqlite.cpp tests/test_agent.cpp tests/CMakeLists.txt
git log --oneline -1
```

---

### Task 5: ProjectIndexService SQL 化 + 删除 IndexManifest

**Files:**
- Rewrite: `src/agent/index/ProjectIndexService.h`、`src/agent/index/ProjectIndexService.cpp`
- Delete: `src/agent/index/IndexManifest.h`、`src/agent/index/IndexManifest.cpp`
- Delete: `tests/test_index_manifest.cpp`
- Modify: `src/AppAssembly.cpp`（构造点）、`cmake/Sources.cmake`、`tests/CMakeLists.txt`（两处列表移除 test_index_manifest）、`tests/test_sqlite_store.cpp`（追加清单表级联测试）

- [ ] **Step 1: 重写头文件**

`src/agent/index/ProjectIndexService.h` 全文替换为：

```cpp
#pragma once

// ProjectIndexService — 基于内容哈希清单的增量索引服务（SQLite 实现）。
//
// 时效性保证（清单存于 novel.db 的 index_sources/index_chunks/kv_store）：
//   - 增量：源内容哈希未变则跳过重嵌入
//   - 孤儿清理：源删除后遗留向量随下次索引移除
//   - 模型指纹：嵌入模型/维度变化时整库失效重建（DROP vec_chunks + 清空清单）
//
// 索引源（source_key 前缀）：
//   chapter:<id>  章节 Markdown 正文（NovelChunker 切分为多 chunk）
//   char:<id>     角色核心信息（单 chunk）
//   setting:<id>  设定核心信息（单 chunk）
//   rule:<id>     世界规则核心信息（单 chunk）
//   memory:<id>   长期记忆条目（单 chunk，可选注入）
//
// 事务模型：indexAll 的整库失效与批量写入均为单事务（经 SqliteStore）。
// 向量写入直连 SQL，不再经 IVectorStore（避免与检索侧共享锁嵌套）。

#include "agent/index/IIndexService.h"

#include <memory>
#include <mutex>

class ProjectAccess;
namespace storage { class SqliteStore; }
namespace retrieval { class IEmbeddingGenerator; }

namespace agent {

class LongTermMemoryStore;

class ProjectIndexService : public IIndexService {
public:
    // @param access 项目受控访问层（P2/P3：索引只读经 withReadLock 快照）。
    // @param sqlite SQLite 单库（清单表与向量表所在库）；非拥有引用，
    //               调用方保证其存活期覆盖本服务生命周期。
    // @param eg 嵌入生成器；非拥有引用，存活期约定同上。
    // @param memory_store 长期记忆日志；非拥有指针，可为 nullptr。
    ProjectIndexService(std::shared_ptr<ProjectAccess> access,
                        storage::SqliteStore& sqlite,
                        retrieval::IEmbeddingGenerator& eg,
                        LongTermMemoryStore* memory_store = nullptr);

    IndexResult indexAll(
        std::function<void(const std::string&)> progress = nullptr,
        bool force = false) override;

private:
    std::shared_ptr<ProjectAccess> project_access_;
    storage::SqliteStore& sqlite_;
    retrieval::IEmbeddingGenerator& embedding_gen_;
    LongTermMemoryStore* memory_store_ = nullptr;
    std::mutex index_mutex_;  // E8：indexAll 内部串行化（多会话完成回调并发调用）
};

} // namespace agent
```

- [ ] **Step 2: 重写实现（单事务 + 直连 SQL）**

`src/agent/index/ProjectIndexService.cpp` 全文替换为：

```cpp
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

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <string>
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

    const std::string model = embedding_gen_.modelName();
    const int dim = embedding_gen_.dimension();

    // ── 模型指纹校验：模型/维度变化或强制时整库失效（单事务）──
    bool wiped = false;
    {
        std::string wipe_reason;
        sqlite_.withLock([&](storage::SqliteStore& s) {
            const std::string saved_model = s.getKV("embedding_model");
            const int saved_dim = parseInt(s.getKV("embedding_dimension"));
            std::map<std::string, PrevEntry> prevs = loadAllPrevEntries(s.db());
            const bool has_sources = !prevs.empty();

            const bool incompatible =
                !saved_model.empty() && saved_model != model;
            const bool dim_changed =
                saved_dim != 0 && dim != 0 && saved_dim != dim;
            const bool orphan_fingerprint =
                saved_model.empty() && has_sources;  // 有源但无指纹（异常态）

            if (force && has_sources) {
                wipe_reason = "强制全量重建索引...";
            } else if (incompatible || dim_changed || orphan_fingerprint) {
                wipe_reason = "嵌入模型已变更 (" + saved_model + " → "
                            + model + ")，整库重建...";
            }

            if (!wipe_reason.empty()) {
                s.inTransaction([&](storage::SqliteStore& s2) {
                    s2.resetVectorTable();   // DROP vec_chunks + 清维度缓存（写入时重建）
                    s2.db().exec("DELETE FROM index_sources");  // 级联清空 index_chunks
                    s2.setKV("embedding_model", "");
                    s2.setKV("embedding_dimension", "");
                });
                wiped = true;
            }
        });
        if (wiped) report(wipe_reason);
    }

    // ── 收集当前项目的全部索引源并读取前次清单快照 ──
    retrieval::NovelChunker chunker;
    std::map<std::string, PendingSource> desired;
    std::vector<std::string> unchanged_keys;

    std::map<std::string, PrevEntry> prevs;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        prevs = loadAllPrevEntries(s.db());
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

    for (const auto& ch : chapters) {
        if (ch.file_path.empty()) continue;
        std::string md = ProjectIO::readChapter(project_path, ch.file_path);
        if (md.empty()) continue;
        ++result.chapters;

        const std::string key = "chapter:" + ch.id;
        const std::string hash = hashContent(md);
        const auto it = prevs.find(key);
        if (it != prevs.end() && it->second.content_hash == hash) {
            unchanged_keys.push_back(key);
            continue;
        }
        PendingSource ps;
        ps.content_hash = hash;
        ps.chunks = chunker.chunkChapter(ch, md);
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
            for (const auto& [key, prev] : prevs) {
                const bool alive = desired.count(key) > 0
                    || std::find(unchanged_keys.begin(), unchanged_keys.end(), key)
                       != unchanged_keys.end();
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
            }
        }

        SQLite::Statement ins_vec(db,
            "INSERT INTO vec_chunks (chunk_id, metadata, embedding_json, embedding)"
            " VALUES (?, ?, ?, ?)");
        for (size_t i = 0; i < chunk_owner.size(); ++i) {
            const auto& [key, ci] = chunk_owner[i];
            const auto& chunk = desired[key].chunks[ci];
            const auto& emb = embeddings[i];
            ins_vec.bind(1, chunk.id);
            ins_vec.bind(2, chunk.metadata.dump());
            nlohmann::json j = emb;
            ins_vec.bind(3, j.dump());
            ins_vec.bind(4, j.dump());
            ins_vec.exec();
        }

        writeFingerprint(s, model, embeddings.front().size());
    });

    report("向量索引已更新: " + std::to_string(result.total_chunks) + " 个片段 ("
         + std::to_string(result.updated_sources) + " 个源) → "
         + project_path + "/.novelagent/novel.db");
    return result;
}

} // namespace agent
```

注意：`wipe_reason` 在 withLock 内赋值、锁外使用——lambda 按引用捕获（同一线程，无并发），`report(wipe_reason)` 在锁外读取，安全。

上面用到的 `SqliteStore::resetVectorTable` 需在 `src/storage/SqliteStore.h/.cpp` 新增（放在 `ensureVectorTable` 附近）：

```cpp
    // 使向量表失效：DROP 表并清零维度缓存（下次 ensureVectorTable 重建）。
    void resetVectorTable();
```

```cpp
void SqliteStore::resetVectorTable()
{
    db_->exec("DROP TABLE IF EXISTS vec_chunks");
    vector_dimension_ = 0;
}
```

WHY：wipe 路径只 DROP 表，不可先 `ensureVectorTable` 再 DROP——若维度缓存已与该维度一致，写入路径的 `ensureVectorTable` 会因缓存命中而不重建表，导致后续 INSERT 落空。`resetVectorTable` 清零缓存保证下一次任何写入都会重建。

- [ ] **Step 3: 删除 IndexManifest 与旧测试，更新构建文件**

```bash
git rm src/agent/index/IndexManifest.h src/agent/index/IndexManifest.cpp tests/test_index_manifest.cpp
```

`cmake/Sources.cmake` NOVELAGENT_AGENT 段删除 IndexManifest 两行。

`tests/CMakeLists.txt` 两处 CORE_TESTS 列表删除 `test_index_manifest`。

`tests/test_sqlite_store.cpp` 追加两个用例（清单表 ON DELETE CASCADE + kv 指纹写入）：

```cpp
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
```

在 `main()` 中 `test_kv_roundtrip()` 之后调用 `test_index_tables_cascade();`。

- [ ] **Step 4: 更新 AppAssembly 构造点**

`src/AppAssembly.cpp:118-119`：

```cpp
    index_service_ = std::make_unique<agent::ProjectIndexService>(
        project_access_, sqlite_store_, embedding_gen_, &ltm_store_);
```

（相应 include 由 NovelAgentApp.h 传递；若编译报未声明，补 `#include "storage/SqliteStore.h"`。）

- [ ] **Step 5: 构建并运行测试**

```bash
cmake --preset default
./scripts/verify.sh "test_sqlite_store|test_agent|test_search_memory_tools"
```

预期：全部通过；test_index_manifest 已从测试列表消失。

- [ ] **Step 6: 提交**

```bash
git add src/agent/index/ProjectIndexService.h src/agent/index/ProjectIndexService.cpp src/storage/SqliteStore.h src/storage/SqliteStore.cpp src/AppAssembly.cpp cmake/Sources.cmake tests/CMakeLists.txt tests/test_sqlite_store.cpp
git commit -m "refactor(index): ProjectIndexService 直连 SQLite 单事务，删除 IndexManifest" -- src/agent/index/ProjectIndexService.h src/agent/index/ProjectIndexService.cpp src/storage/SqliteStore.h src/storage/SqliteStore.cpp src/AppAssembly.cpp cmake/Sources.cmake tests/CMakeLists.txt tests/test_sqlite_store.cpp
git log --oneline -1
```

（`git rm` 已暂存的删除文件会随 commit 一并提交——task 提交命令需确认删除项已被 `--` 路径列表涵盖：`src/agent/index/IndexManifest.h src/agent/index/IndexManifest.cpp tests/test_index_manifest.cpp` 须加入 `git add` 与 commit 路径。若 `git rm` 已暂存，则上述 commit 路径列表**追加**这三个文件。）

---

### Task 6: 组装切换 + 删除旧 VectorStore + 旧文件清理 + 配套更新

**Files:**
- Modify: `src/NovelAgentApp.h`、`src/NovelAgentApp.cpp`、`src/AppAssembly.cpp`（向量库初始化段）
- Modify: `src/retrieval/IVectorStore.h`（注释）
- Modify: `tests/test_retrieval.cpp`（全部 VectorStore 用例改 SqliteVectorStore）
- Delete: `src/retrieval/VectorStore.h`、`src/retrieval/VectorStore.cpp`
- Modify: `cmake/Sources.cmake`（NOVELAGENT_RETRIEVAL 移除旧文件——Task 3 已换，此处仅确认）
- Modify: `CHANGELOG.md`

- [ ] **Step 1: NovelAgentApp 成员与装配切换**

`src/NovelAgentApp.h` 修改：

- include 行 12 的 `#include "retrieval/VectorStore.h"` 替换为：

```cpp
#include "retrieval/SqliteVectorStore.h"
#include "storage/SqliteStore.h"
```

- 成员区（原行 54-56）顺序变为（**顺序即构造顺序，sqlite_store_ 必须在 vector_store_/persistence_ 之前**）：

```cpp
    FileStorageBackend storage_;                     // 文件存储后端（绑定项目路径）
    storage::SqliteStore sqlite_store_;              // SQLite 单库（novel.db）
    retrieval::SqliteVectorStore vector_store_{sqlite_store_};   // 向量存储（SQLite 后端）
    agent::SessionPersistence persistence_{sqlite_store_, storage_};  // 会话持久化（SQLite）
```

- 构造函数声明保持；`~NovelAgentApp();` 保持（.cpp 实现改为显式 close）。

`src/NovelAgentApp.cpp` 修改：

- 构造函数初始化列表删除 `, persistence_(storage_)`（默认成员初始化已覆盖）：

```cpp
NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project)
    : client_(provider)
    , agent_(client_, registry_)
    , project_(project ? std::move(project) : std::make_shared<Project>())
    , project_access_(std::make_shared<ProjectAccess>(project_))
    , storage_(project_access_ ? project_access_->path() : "")
    , embedding_gen_(provider)
    , rules_provider_(utils::file::configDir())
{
    setupAgent();
}
```

- 析构函数改为：

```cpp
NovelAgentApp::~NovelAgentApp()
{
    sqlite_store_.close();
}
```

`src/AppAssembly.cpp`：

- `setupPersistenceAndVectorStore()`（约行 91-102）全文替换为：

```cpp
void NovelAgentApp::setupPersistenceAndVectorStore()
{
    if (project_access_ && !project_access_->path().empty()) {
        const std::string agent_dir = project_access_->path() + "/.novelagent";
        // 旧文件布局清理（无兼容要求：全新库，旧内容直接删除）
        removeLegacyStorageFiles(agent_dir);
        sqlite_store_.open(agent_dir + "/novel.db");
    }

    // 仅库可用时启用持久化（避免空路径时写到盘符根目录/.novelagent）
    if (sqlite_store_.isOpen()) {
        agent_.setPersistence(&persistence_);
    }
}
```

- 文件顶部匿名命名空间新增（放在 `#include` 之后、namespace 之前）：

```cpp
namespace {

// 清理旧 JSON 文件布局（vectors.json/index_manifest.json/sessions/archive）。
// 程序尚未发布，无兼容要求；存在即删除并记日志。
void removeLegacyStorageFiles(const std::string& agent_dir)
{
    namespace fs = std::filesystem;
    auto remove_if_exists = [](const fs::path& p) {
        std::error_code ec;
        fs::remove_all(p, ec);
        if (ec) {
            spdlog::warn("[NovelAgentApp] 清理旧存储文件失败: {} ({})",
                         p.string(), ec.message());
        }
    };
    const fs::path dir(agent_dir);
    remove_if_exists(dir / "vectors.json");
    remove_if_exists(dir / "index_manifest.json");
    remove_if_exists(dir / "sessions");
    remove_if_exists(dir / "archive");
}

} // namespace
```

- include 补充：`#include <filesystem>`、`#include <system_error>`、`#include <spdlog/spdlog.h>`（若无）。

- [ ] **Step 2: IVectorStore.h 注释更新**

`src/retrieval/IVectorStore.h:3-6`（顶部注释段与"未来可选"注释）替换为：

```cpp
// 向量存储抽象接口 — 解耦语义搜索与具体存储后端。
//
// 当前实现：SqliteVectorStore（SQLite + sqlite-vec vec0 虚拟表）
//
// 所有依赖向量搜索的模块均通过此接口交互，替换后端不影响上层代码。
```

- [ ] **Step 3: 重写 test_retrieval.cpp 的 VectorStore 用例**

`tests/test_retrieval.cpp` 修改：

- include 段（原行 1）替换为：

```cpp
#include "retrieval/SqliteVectorStore.h"
#include "retrieval/NovelChunker.h"
#include "storage/SqliteStore.h"
#include "project/Models.h"
#include "utils/FileUtils.h"
```

- 新增辅助（放到 tmpPath/cleanup 附近）：

```cpp
// 打开临时 SQLite 库并绑定 SqliteVectorStore。
struct VectorFixture {
    storage::SqliteStore db;
    retrieval::SqliteVectorStore store;
    explicit VectorFixture(const std::string& path) : store(db) { db.open(path); }
};
```

- 全部 10 个 VectorStore 用例按下列模式改写（以 `test_vector_store_insert_and_search` 为例，其余相同模式）：

```cpp
void test_vector_store_insert_and_search() {
    TEST("VectorStore — 插入和搜索");

    const std::string db_path = tmpPath("tmp_test_vs_search.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    auto v1 = makeVec(4, 1.0f);
    auto v2 = makeVec(4, -1.0f);
    auto v3 = makeVec(4, 0.5f);

    fx.store.insert("id-1", v1, {{"label", "positive"}});
    fx.store.insert("id-2", v2, {{"label", "negative"}});
    fx.store.insert("id-3", v3, {{"label", "neutral"}});

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

    fx.db.close();
    cleanup(db_path);
    PASS();
}
```

原用例逐一改写要点：
- `retrieval::VectorStore store; store.init(db_path);` → `VectorFixture fx(db_path);`
- 所有 `store.` → `fx.store.`；`store.close()` → `fx.db.close()`
- `test_vector_store_init_empty` → 用 `fx.store.count()==0`（新建库 count 0）
- `test_vector_store_flush_persists` → 语义改为"事务即持久化"：insert 后 close，重开 Fixture 断言 count==1（不再需要 flush）
- `test_vector_store_flush_clears_dirty` → **删除**（文件落盘语义不复存在），替换为 `test_vector_store_flush_noop`（flush() 调用不抛异常）：

```cpp
void test_vector_store_flush_noop() {
    TEST("VectorStore::flush — no-op 不抛异常（事务即持久化）");
    const std::string db_path = tmpPath("tmp_test_vs_flushnoop.db");
    cleanup(db_path);
    VectorFixture fx(db_path);
    fx.store.insert("flush-1", {0.1f, 0.2f}, {{"k", "v"}});
    fx.store.flush();
    CHECK(fx.store.count() == 1);
    fx.db.close();
    cleanup(db_path);
    PASS();
}
```

- `test_vector_store_concurrent_flush` → flush 线程保留（转发 no-op），断言不变
- `test_vector_store_persistence`（用 `store.get()`）→ Fixture 两段式重开，断言不变（get 语义由 SqliteVectorStore::get 提供）
- main() 中调用列表同步（删 dirty 用例、加 flush_noop）

- [ ] **Step 4: 删除旧 VectorStore 文件并移除构建引用**

```bash
git rm src/retrieval/VectorStore.h src/retrieval/VectorStore.cpp
```

`cmake/Sources.cmake`：确认 NOVELAGENT_RETRIEVAL 已不含 VectorStore（Task 3 已换为 SqliteVectorStore）。

- [ ] **Step 5: CHANGELOG 增量记录**

`CHANGELOG.md` 顶部（`# Changelog` 之后）新增：

```markdown
## [2026-08-17] 存储层迁入 SQLite 单库（Phase 1：会话/向量/索引清单）

### 重构 — SQLite 集成
- 新增 `third_party/`：sqlite3 amalgamation（3.46.1，开 FTS5）+ sqlite-vec v0.1.6 + SQLiteCpp 3.4.0，全部 vendor 随仓库构建。
- 新增 `src/storage/SqliteStore`：`<项目>/.novelagent/novel.db` 唯一入口（建表迁移/WAL/事务/损坏自愈/全库互斥锁）。
- `VectorStore`（JSON 暴力扫描）删除 → `SqliteVectorStore`（sqlite-vec vec0），`IVectorStore` 接口不变。
- `IndexManifest` 删除 → 清单表 `index_sources/index_chunks/kv_store`，`ProjectIndexService` 直连 SQL、`indexAll` 单事务提交。
- `SessionPersistence` 公开接口不变，内部改 `sessions/messages/message_history` 表；删除会话置 `archived=1`（原 archive/ 归档语义）。
- 旧文件布局（vectors.json/index_manifest.json/sessions/archive）启动时清理，不做数据迁移（未发布）。
```

- [ ] **Step 6: 构建 + 聚焦验证**

```bash
cmake --preset default
./scripts/verify.sh "test_retrieval|test_sqlite|test_session_sqlite|test_agent"
```

预期：全部通过。

- [ ] **Step 7: 提交**

```bash
git add src/NovelAgentApp.h src/NovelAgentApp.cpp src/AppAssembly.cpp src/retrieval/IVectorStore.h tests/test_retrieval.cpp CHANGELOG.md
git commit -m "refactor: 组装切换 SqliteVectorStore/novel.db，删除旧 VectorStore 与旧文件布局，启动清理遗留文件" -- src/NovelAgentApp.h src/NovelAgentApp.cpp src/AppAssembly.cpp src/retrieval/IVectorStore.h tests/test_retrieval.cpp CHANGELOG.md
git log --oneline -1
```

（`git rm` 的 `src/retrieval/VectorStore.h/.cpp` 已暂存，随本提交的 `--` 路径列表**追加**这两个路径。）

---

### Task 7: 全量回归与收尾

**Files:**
- 验证：全量构建 + 全量测试

- [ ] **Step 1: 全量回归**

```bash
./scripts/verify.sh
```

预期：全部测试通过（含 GUI 目标构建）。失败则按 `--output-on-failure` 输出定位修复后复验。

- [ ] **Step 2: 语法/一致性抽查**

```bash
git grep -n "VectorStore\.h\|IndexManifest\|vectors\.json\|index_manifest" -- src tests cmake CMakeLists.txt || echo "无残留引用"
```

预期：除注释/文档中的历史说明外无源码引用残留（`git grep` 输出为空或仅 docs/）。

- [ ] **Step 3: 确认提交链**

```bash
git log --oneline -8
```

预期：Tasks 1-6 的提交在列，且不包含用户 WIP 文件。

---

## 自审记录（plan 编写时）

- **规格覆盖**：会话/消息（Task 4）、向量表（Task 2/3）、索引清单（Task 5）、旧文件清理（Task 6）、Phase2 FTS5（明确不在范围内）、SQLiteCpp 编译形态（Task 1）、seq 编号（Task 4 代码）、listSessions archived 过滤 + updated_at DESC（Task 4）、损坏自愈（Task 2）、异常策略（SqliteStore 头注释）、生命周期（Task 6 装配）、vec0 spike 核验（Task 2）。
- **已知偏差**：vec0 附加列增加 `embedding_json`（规格已补一行说明）；`listSessions` 排序按规格改为 updated_at DESC（原文件版由前端排序，行为兼容）。
- **风险与控制**：
  - sqlite-vec 具体行为（k 参数名、附加列过滤、SQLiteCpp 绑定/取值细节）以 Task 2 spike 实测为准，测试驱动修正。
  - `wipe` 路径依赖 `SqliteStore::resetVectorTable`（Task 5 Step 2 已内联进代码块并附 WHY）。
  - 共享 SqliteStore 的嵌套加锁风险：索引服务直连 SQL（不经 SqliteVectorStore），已在 Task 5 头文件注释与 Task 3 头文件注释双重说明。
  - 不依赖 `SQLite::Statement::exec()` 的变更计数返回值（SQLiteCpp 版本行为未锁定）：所有"删没删到"判断均先 SELECT 确认（Task 3/4 代码）。
  - 自审修正记录：wipe 事务删掉 ensureVectorTable+DROP 两段式（缓存命中 bug）；desired-empty 路径补写指纹（对齐旧 setModelFingerprint 时机）；test_update 断言修正；test_sqlite_store 补 includes。
- **类型一致性**：`SqliteStore::withLock/inTransaction/exec/getKV/setKV/ensureVectorTable/resetVectorTable/db`、`SqliteVectorStore::get` 在各 Task 中签名一致；`SessionPersistence(SqliteStore&, FileStorageBackend&)` 在最终装配（Task 6）与测试（Task 4）中一致。