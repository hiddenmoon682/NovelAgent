// 测试 src/project/ProjectIO.h 和 src/project/ProjectManager.h。

#include "project/ProjectIO.h"
#include "project/ProjectManager.h"
#include "project/Models.h"
#include "utils/FileUtils.h"

#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { tests_run++; std::cout << "  TEST " << (name) << " ... "; } while(0)
#define PASS() \
    do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << (msg) << '\n'; return; } while(0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); } } while(0)

// 测试过程中使用的临时项目目录。
const std::string kTestDir = "__test_project_io_tmp";

// 清理测试临时目录，避免前一次测试残留影响结果。
void cleanup() {
    if (utils::file::exists(kTestDir)) {
        utils::file::removeDir(kTestDir);
    }
}

// 验证 createProjectDir 会创建完整目录结构。
void test_createProjectDir() {
    TEST("createProjectDir 目录结构");
    cleanup();

    ProjectIO::createProjectDir(kTestDir, "测试小说");

    CHECK(utils::file::isDir(kTestDir));
    CHECK(utils::file::isDir(utils::file::joinPath(kTestDir, "chapters")));
    CHECK(utils::file::isDir(utils::file::joinPath(kTestDir, ".novelagent")));

    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "novel.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "outline.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "characters.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "settings.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "style.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, ".novelagent/conversation.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, ".novelagent/summaries.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, ".novelagent/state.json")));

    PASS();
}

// 验证 createProjectDir 不会覆盖用户手动修改过的文件。
void test_createProjectDir_idempotent() {
    TEST("createProjectDir 不覆盖已有文件");

    std::string novelPath = utils::file::joinPath(kTestDir, "novel.json");
    utils::file::writeText(novelPath, "// modified manually");

    ProjectIO::createProjectDir(kTestDir, "新标题");

    std::string content = utils::file::readText(novelPath);
    CHECK(content == "// modified manually");

    PASS();
}

// 验证 Project 保存再加载后的关键字段保持一致。
void test_save_load_roundtrip() {
    TEST("Project save -> load 往返一致");

    Project orig;
    orig.format_version = 1;
    orig.title = "上古之影";
    orig.author = "测试作者";
    orig.description = "一场关于发现与冒险的奇幻旅程";
    orig.genre = {"fantasy", "epic"};
    orig.target_word_count = 100000;
    orig.current_word_count = 12345;
    orig.status = "in_progress";
    orig.pov = "third_person_limited";
    orig.tense = "past";

    orig.outline.premise = "一位年轻学者发现了上古神器，改变了世界";
    PlotThread pt{"pt-main", "主线", "追寻神器真相"};
    orig.outline.plot_threads.push_back(pt);

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "发现";
    ch.order = 1;
    ch.synopsis = "主人公在图书馆发现神器";
    ch.status = "drafted";
    ch.word_count = 3500;
    ch.file_path = "chapters/001-discovery.md";
    ch.scenes = {"图书馆日常", "地震", "发现密室", "接触神器"};
    ch.pov_characters = {"elena"};
    ch.key_events = {"artifact_discovered"};
    ch.themes = {"discovery", "mystery"};
    orig.outline.chapters.push_back(ch);

    Character hero;
    hero.id = "elena";
    hero.name = "Elena Vasquez";
    hero.role = "protagonist";
    hero.traits = {"intelligent", "brave"};
    hero.relationships = {{"marcus", "mentor"}};
    hero.chapter_appearances = {"ch-001"};
    orig.characters.push_back(hero);

    Character mentor;
    mentor.id = "marcus";
    mentor.name = "Marcus Chen";
    mentor.role = "supporting";
    orig.characters.push_back(mentor);

    Setting loc;
    loc.id = "library";
    loc.name = "Thorne 大学图书馆";
    loc.category = "location";
    loc.description = "古老的图书馆，地下有密室";
    loc.attributes = {{"founded", "1642"}, {"architecture", "哥特复兴式"}};
    orig.settings.push_back(loc);

    orig.style.tone = "atmospheric";
    orig.style.pacing = "moderate";
    orig.style.chapter_length_target = 4000;

    orig.path = kTestDir;
    ProjectIO::save(orig);

    Project loaded = ProjectIO::load(kTestDir);

    CHECK(loaded.format_version == orig.format_version);
    CHECK(loaded.title == "上古之影");
    CHECK(loaded.author == "测试作者");
    CHECK(loaded.description == "一场关于发现与冒险的奇幻旅程");
    CHECK(loaded.genre.size() == 2);
    CHECK(loaded.genre[0] == "fantasy");
    CHECK(loaded.genre[1] == "epic");
    CHECK(loaded.target_word_count == 100000);
    CHECK(loaded.current_word_count == 12345);
    CHECK(loaded.status == "in_progress");
    CHECK(loaded.pov == "third_person_limited");
    CHECK(loaded.tense == "past");

    CHECK(loaded.outline.premise == orig.outline.premise);
    CHECK(loaded.outline.plot_threads.size() == 1);
    CHECK(loaded.outline.plot_threads[0].name == "主线");
    CHECK(loaded.outline.chapters.size() == 1);
    CHECK(loaded.outline.chapters[0].title == "发现");
    CHECK(loaded.outline.chapters[0].word_count == 3500);
    CHECK(loaded.outline.chapters[0].scenes.size() == 4);

    CHECK(loaded.characters.size() == 2);
    CHECK(loaded.characters[0].name == "Elena Vasquez");
    CHECK(loaded.characters[0].role == "protagonist");
    CHECK(loaded.characters[0].relationships["marcus"] == "mentor");
    CHECK(loaded.characters[1].name == "Marcus Chen");

    CHECK(loaded.settings.size() == 1);
    CHECK(loaded.settings[0].name == "Thorne 大学图书馆");
    CHECK(loaded.settings[0].attributes["founded"] == "1642");

    CHECK(loaded.style.tone == "atmospheric");
    CHECK(loaded.style.chapter_length_target == 4000);

    PASS();
}

// 验证章节 Markdown 的写入和读取流程。
void test_chapter_read_write() {
    TEST("章节 Markdown 读写");

    std::string chapterPath = "chapters/01-test.md";
    std::string content = "# 第一章\n\n这是测试内容。\n\n## 场景一\n\n第一场戏。\n";

    ProjectIO::writeChapter(kTestDir, chapterPath, content);

    std::string fullPath = utils::file::joinPath(kTestDir, chapterPath);
    CHECK(utils::file::exists(fullPath));

    std::string read = ProjectIO::readChapter(kTestDir, chapterPath);
    CHECK(read == content);

    PASS();
}

// 验证读取缺失章节时返回空字符串。
void test_chapter_read_missing() {
    TEST("读取不存在章节返回空字符串");

    std::string result = ProjectIO::readChapter(kTestDir, "chapters/does-not-exist.md");
    CHECK(result.empty());

    PASS();
}

// 验证对话历史的追加和加载逻辑。
void test_conversation() {
    TEST("对话历史追加与加载");

    ProjectIO::saveConversation(kTestDir, nlohmann::json::array());

    ProjectIO::appendConversation(kTestDir, "user", "我想写第一章");
    ProjectIO::appendConversation(kTestDir, "assistant", "好的，让我帮你写第一章。");
    ProjectIO::appendConversation(kTestDir, "user", "请用第三人称");

    auto conv = ProjectIO::loadConversation(kTestDir);

    CHECK(conv.is_array());
    CHECK(conv.size() == 3);
    CHECK(conv[0]["role"] == "user");
    CHECK(conv[0]["content"] == "我想写第一章");
    CHECK(conv[1]["role"] == "assistant");
    CHECK(conv[2]["role"] == "user");
    CHECK(conv[2]["content"] == "请用第三人称");

    PASS();
}

// 验证 loadJsonFile 对缺失、空文件和坏 JSON 的处理。
void test_loadJsonFile_edge_cases() {
    TEST("loadJsonFile 异常情况");

    auto missing = ProjectIO::loadJsonFile(kTestDir + "/_no_such_file_.json");
    CHECK(!missing.has_value());

    std::string emptyPath = utils::file::joinPath(kTestDir, "_empty.json");
    utils::file::writeText(emptyPath, "");
    auto empty = ProjectIO::loadJsonFile(emptyPath);
    CHECK(!empty.has_value());
    utils::file::removeFile(emptyPath);

    std::string badPath = utils::file::joinPath(kTestDir, "_bad.json");
    utils::file::writeText(badPath, "这不是合法的 JSON {{{");
    auto bad = ProjectIO::loadJsonFile(badPath);
    CHECK(!bad.has_value());
    utils::file::removeFile(badPath);

    PASS();
}

// 验证 ProjectManager::create 的初始化行为。
void test_pm_create() {
    TEST("ProjectManager create");
    cleanup();
    ProjectManager pm;
    Project p = pm.create(kTestDir, "测试小说");
    CHECK(p.title == "测试小说");
    CHECK(p.path == kTestDir);
    CHECK(!p.created.empty());
    CHECK(!p.modified.empty());
    CHECK(p.status == "planning");
    CHECK(utils::file::isDir(utils::file::joinPath(kTestDir, "chapters")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "novel.json")));
    PASS();
}

// 验证 ProjectManager::open 能读取已有项目。
void test_pm_open() {
    TEST("ProjectManager open 已有项目");
    ProjectManager pm;
    Project p = pm.open(kTestDir);
    CHECK(p.title == "测试小说");
    CHECK(p.path == kTestDir);
    PASS();
}

// 验证打开无效目录时会返回空 Project。
void test_pm_open_invalid() {
    TEST("ProjectManager 打开无效目录返回空 Project");
    ProjectManager pm;
    Project p = pm.open(kTestDir + "/_no_such_");
    CHECK(p.title.empty());
    PASS();
}

// 验证 openOrCreate 在目录不存在时会创建项目。
void test_pm_openOrCreate_new() {
    TEST("ProjectManager openOrCreate 新建");
    std::string newDir = kTestDir + "/sub-dir/new-project";
    ProjectManager pm;
    Project p = pm.openOrCreate(newDir, "新建小说");
    CHECK(p.title == "新建小说");
    CHECK(p.path == newDir);
    CHECK(utils::file::exists(utils::file::joinPath(newDir, "novel.json")));
    PASS();
}

// 验证 openOrCreate 在目录已存在时会直接打开。
void test_pm_openOrCreate_existing() {
    TEST("ProjectManager openOrCreate 打开已存在");
    std::string newDir = kTestDir + "/sub-dir/new-project";
    ProjectManager pm;
    Project p = pm.openOrCreate(newDir, "不应该用这个标题");
    CHECK(p.title == "新建小说");
    PASS();
}

// 验证 isValid 对不同目录状态的判断。
void test_pm_isValid() {
    TEST("ProjectManager isValid");
    ProjectManager pm;
    CHECK(pm.isValid(kTestDir));
    CHECK(!pm.isValid(kTestDir + "/_ghost_"));
    std::string emptyDir = kTestDir + "/empty-subdir";
    utils::file::createDirs(emptyDir);
    CHECK(!pm.isValid(emptyDir));
    PASS();
}

// 验证 listProjects 只返回有效项目目录。
void test_pm_listProjects() {
    TEST("ProjectManager listProjects");
    ProjectManager pm;

    std::string parentDir = kTestDir + "/list-test";
    utils::file::createDirs(parentDir);
    ProjectIO::createProjectDir(parentDir + "/proj-a", "项目A");
    ProjectIO::createProjectDir(parentDir + "/proj-b", "项目B");
    utils::file::createDirs(parentDir + "/not-a-project");

    auto projects = pm.listProjects(parentDir);
    CHECK(projects.size() == 2);
    PASS();
}

// 验证标题到默认目录名的转换规则。
void test_pm_getDefaultProjectDir() {
    TEST("ProjectManager getDefaultProjectDir");
    CHECK(ProjectManager::getDefaultProjectDir("The Shadow of Ancients") == "The-Shadow-of-Ancients");
    CHECK(!ProjectManager::getDefaultProjectDir("上古之影").empty());
    CHECK(ProjectManager::getDefaultProjectDir("") == "untitled-novel");
    PASS();
}

int main() {
    std::cout << "=== test_project_io ===\n\n";

    test_createProjectDir();
    test_createProjectDir_idempotent();
    test_save_load_roundtrip();
    test_chapter_read_write();
    test_chapter_read_missing();
    test_conversation();
    test_loadJsonFile_edge_cases();

    test_pm_create();
    test_pm_open();
    test_pm_open_invalid();
    test_pm_openOrCreate_new();
    test_pm_openOrCreate_existing();
    test_pm_isValid();
    test_pm_listProjects();
    test_pm_getDefaultProjectDir();

    cleanup();

    std::cout << '\n';
    std::cout << "结果: " << tests_passed << '/' << tests_run << " 通过\n";

    if (tests_passed == tests_run) {
        std::cout << "All tests passed!\n";
        return 0;
    } else {
        std::cout << (tests_run - tests_passed) << " 个测试失败\n";
        return 1;
    }
}
