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

    std::string long_line;
    for (int i = 0; i < 40; ++i) long_line += "长";  // 40 个中文字符 = 120 字节
    llm::Memory mem;
    mem.addUser(long_line + "\n第二行");
    persistence.save("s-1", mem);

    auto sessions = persistence.listSessions();
    CHECK(sessions.size() == 1);
    CHECK(sessions[0].id == "s-1");
    CHECK(!sessions[0].title.empty());
    // 截断标记：标题以省略号（3 字节 UTF-8）结尾
    CHECK(sessions[0].title.size() > 3);
    CHECK(sessions[0].title.compare(sessions[0].title.size() - 3, 3, "…") == 0);

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
    // 先登记会话（message_history 外键引用 sessions，session 须先存在）
    llm::Memory empty_mem;
    persistence.save("s-9", empty_mem);
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