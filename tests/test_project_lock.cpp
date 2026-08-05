// test_project_lock — ProjectAccess 受控访问层测试（P2/P3 定版）。
//
// 锁集中在 ProjectAccess（Project 是纯数据模型）。覆盖：
// 1. 快照读（getXxx）返回拷贝，不受后续修改影响
// 2. 事务方法（add/update/remove）行为 + 自动 markDirty
// 3. withReadLock / withWriteLock 兜底
// 4. 并发：多线程经同一 access 事务写无数据丢失；读写混合不崩溃
// 5. save() 锁内快照 + 脏标记落盘后可重新加载

#include "project/Models/Project.h"
#include "project/ProjectAccess.h"
#include "project/ProjectIO.h"

#include <atomic>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while(0)

// =========================================================================
// 1. 快照读返回拷贝
// =========================================================================

void test_snapshot_is_copy() {
    TEST("快照读返回拷贝（改原数据不影响快照）");
    Project p;
    Character c;
    c.id = "char-001";
    c.name = "主角";
    p.characters.push_back(c);
    ProjectAccess access(p);

    auto snap = access.getCharacters();      // 快照
    CHECK(snap.size() == 1);
    p.characters[0].name = "改名";           // 直接改原始数据
    p.characters.push_back(c);
    CHECK(snap.size() == 1);                 // 快照不受影响
    CHECK(snap[0].name == "主角");

    auto outline_snap = access.getOutline();
    CHECK(outline_snap.chapters.empty());

    auto style_snap = access.getStyle();
    CHECK(style_snap.tone == "neutral");
    PASS();
}

// =========================================================================
// 2. 事务方法行为 + 自动 markDirty
// =========================================================================

void test_transaction_methods() {
    TEST("add/update/remove 事务方法 + 自动 markDirty");
    Project p;
    ProjectAccess access(p);

    // addCharacter
    Character c;
    c.id = "char-001";
    c.name = "主角";
    access.addCharacter(c);
    CHECK(p.characters.size() == 1);
    CHECK(p.isDirty(Project::DIRTY_CHARACTERS));

    // updateCharacter
    bool called = false;
    CHECK(access.updateCharacter("char-001", [&](Character& ch) {
        called = true;
        ch.name = "新名字";
    }));
    CHECK(called);
    CHECK(p.characters[0].name == "新名字");
    // 不存在的 id → false，lambda 不调用
    CHECK(!access.updateCharacter("char-999", [](Character&) {}));

    // removeCharacter
    CHECK(access.removeCharacter("char-001"));
    CHECK(p.characters.empty());
    CHECK(!access.removeCharacter("char-001"));

    // addChapter / updateChapter / removeChapter（outline 域）
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    access.addChapter(ch);
    CHECK(p.outline.chapters.size() == 1);
    CHECK(p.isDirty(Project::DIRTY_OUTLINE));
    CHECK(access.updateChapter("ch-001", [](Chapter& x) { x.title = "改"; }));
    CHECK(p.outline.chapters[0].title == "改");
    CHECK(access.removeChapter("ch-001"));
    CHECK(p.outline.chapters.empty());

    // addVolume / updateVolume / removeVolume
    Volume v;
    v.id = "vol-001";
    v.title = "第一卷";
    access.addVolume(v);
    CHECK(p.outline.volumes.size() == 1);
    CHECK(access.updateVolume("vol-001", [](Volume& x) { x.title = "卷改"; }));
    CHECK(p.outline.volumes[0].title == "卷改");
    CHECK(access.removeVolume("vol-001"));

    // addPlotThread / updatePlotThread / removePlotThread
    PlotThread pt;
    pt.id = "pt-001";
    pt.name = "主线";
    access.addPlotThread(pt);
    CHECK(p.outline.plot_threads.size() == 1);
    CHECK(access.updatePlotThread("pt-001", [](PlotThread& x) { x.name = "线改"; }));
    CHECK(p.outline.plot_threads[0].name == "线改");
    CHECK(access.removePlotThread("pt-001"));

    // addSetting / updateSetting / removeSetting
    Setting s;
    s.id = "setting-001";
    s.name = "地点";
    access.addSetting(s);
    CHECK(p.settings.size() == 1);
    CHECK(p.isDirty(Project::DIRTY_SETTINGS));
    CHECK(access.updateSetting("setting-001", [](Setting& x) { x.name = "改"; }));
    CHECK(access.removeSetting("setting-001"));

    // addWorldRule / updateWorldRule / removeWorldRule
    WorldRule r;
    r.id = "rule-001";
    r.name = "规则";
    access.addWorldRule(r);
    CHECK(p.world_rules.size() == 1);
    CHECK(p.isDirty(Project::DIRTY_WORLD_RULES));
    CHECK(access.updateWorldRule("rule-001", [](WorldRule& x) { x.name = "改"; }));
    CHECK(access.removeWorldRule("rule-001"));

    // updateStyle
    access.updateStyle([](Style& st) { st.tone = "黑暗史诗"; });
    CHECK(p.style.tone == "黑暗史诗");
    CHECK(p.isDirty(Project::DIRTY_STYLE));
    PASS();
}

// =========================================================================
// 3. withReadLock / withWriteLock 兜底
// =========================================================================

void test_with_lock() {
    TEST("withReadLock / withWriteLock 基本行为");
    Project p;
    p.title = "标题";
    ProjectAccess access(p);

    std::string read = access.withReadLock([&](const Project& pr) { return pr.title; });
    CHECK(read == "标题");

    bool executed = false;
    access.withWriteLock([&](Project& pr) {
        executed = true;
        pr.title = "锁内修改";
    });
    CHECK(executed);
    CHECK(p.title == "锁内修改");
    PASS();
}

// =========================================================================
// 4. 并发：多线程经同一 access 事务写无数据丢失
// =========================================================================

void test_concurrent_writes() {
    TEST("并发事务写：4 线程 × 250 次 addCharacter 无丢失");
    Project p;
    ProjectAccess access(p);
    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                Character c;
                c.id = "char-" + std::to_string(t) + "-" + std::to_string(i);
                c.name = "角色" + std::to_string(t) + "-" + std::to_string(i);
                access.addCharacter(c);
            }
        });
    }
    for (auto& th : threads) th.join();

    CHECK(p.characters.size() == static_cast<size_t>(kThreads * kPerThread));
    PASS();
}

// =========================================================================
// 5. 并发：读写混合（读快照 + updateCharacter）不崩溃且最终一致
// =========================================================================

void test_concurrent_read_write() {
    TEST("并发读写混合：更新与快照读并行，无崩溃、无丢失");
    Project p;
    ProjectAccess access(p);
    constexpr int kChars = 64;
    for (int i = 0; i < kChars; ++i) {
        Character c;
        c.id = "char-" + std::to_string(i);
        c.name = "初始" + std::to_string(i);
        p.characters.push_back(c);
    }

    std::atomic<bool> stop{false};
    std::atomic<int> read_errors{0};
    std::vector<std::thread> threads;

    // 3 个读线程：循环取快照并校验内部一致性（id 不重复）
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&] {
            while (!stop.load()) {
                auto snap = access.getCharacters();
                std::set<std::string> ids;
                for (const auto& c : snap) {
                    if (!ids.insert(c.id).second) read_errors++;
                }
            }
        });
    }
    // 2 个写线程：不断更新随机角色（不存在则跳过）
    threads.emplace_back([&] {
        for (int i = 0; i < 2000; ++i) {
            access.updateCharacter("char-" + std::to_string(i % kChars),
                                   [](Character& ch) { ch.name += "."; });
        }
    });
    threads.emplace_back([&] {
        for (int i = 0; i < 2000; ++i) {
            Character c;
            c.id = "char-new-" + std::to_string(i);
            c.name = "新增";
            access.addCharacter(c);
        }
    });

    stop.store(true);
    for (auto& th : threads) th.join();

    CHECK(read_errors.load() == 0);
    CHECK(p.characters.size() == static_cast<size_t>(kChars + 2000));
    PASS();
}

// =========================================================================
// 6. save() 落盘后可重新加载
// =========================================================================

void test_save_reload() {
    TEST("save() 锁内快照落盘，重新加载能看到全部修改");
    namespace fs = std::filesystem;
    const std::string path =
        (fs::temp_directory_path() / "novelagent_test_access").string();
    std::error_code ec;
    fs::remove_all(path, ec);
    ProjectIO::createProjectDir(path, "访问层测试");
    auto project = std::make_shared<Project>(ProjectIO::load(path));

    ProjectAccess access(project);

    // 事务方法
    Character c;
    c.id = "char-001";
    c.name = "主角";
    access.addCharacter(c);
    CHECK(project->characters.size() == 1);
    CHECK(access.updateCharacter("char-001", [](Character& ch) { ch.name = "改"; }));
    CHECK(access.getCharacters().size() == 1);

    // withLock：lambda 接收 Project&（锁内）
    bool read_ok = access.withReadLock([&](const Project& pr) {
        return pr.title == "访问层测试";
    });
    CHECK(read_ok);
    access.withWriteLock([&](Project& pr) {
        pr.current_word_count = 1234;
        pr.markDirty(Project::DIRTY_NOVEL);
    });
    CHECK(project->current_word_count == 1234);

    // save 落盘：重新加载能看到全部修改
    access.save();
    Project reloaded = ProjectIO::load(path);
    CHECK(reloaded.title == "访问层测试");
    CHECK(reloaded.characters.size() == 1);
    CHECK(reloaded.characters[0].name == "改");
    CHECK(reloaded.current_word_count == 1234);

    fs::remove_all(path, ec);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_project_lock ===\n\n";
    test_snapshot_is_copy();
    test_transaction_methods();
    test_with_lock();
    test_concurrent_writes();
    test_concurrent_read_write();
    test_save_reload();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}