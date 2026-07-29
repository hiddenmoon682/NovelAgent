#include "project/Models.h"
#include "project/FileStorageBackend.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"
#include "utils/FileUtils.h"

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        ++tests_run; \
        std::cout << "  TEST " << (name) << " ... "; \
    } while (0)

#define PASS() \
    do { \
        ++tests_passed; \
        std::cout << "PASSED\n"; \
    } while (0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << (msg) << '\n'; \
        return; \
    } while (0)

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            FAIL(#cond); \
        } \
    } while (0)

const std::string kTestDir = "__test_project_io_tmp";

void cleanup() {
    if (utils::file::exists(kTestDir)) {
        utils::file::removeDir(kTestDir);
    }
}

void test_createProjectDir() {
    TEST("createProjectDir");
    cleanup();

    ProjectIO::createProjectDir(kTestDir, "Test Novel");

    CHECK(utils::file::isDir(kTestDir));
    CHECK(utils::file::isDir(utils::file::joinPath(kTestDir, "chapters")));
    CHECK(utils::file::isDir(utils::file::joinPath(kTestDir, ".novelagent")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "novel.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "outline.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "characters.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "settings.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "world_rules.json")));
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, "style.json")));

    auto novelJson = ProjectIO::loadJsonFile(utils::file::joinPath(kTestDir, "novel.json"));
    CHECK(novelJson.has_value());
    CHECK((*novelJson)["format_version"] == 4);
    CHECK((*novelJson)["metadata"].is_object());

    PASS();
}

void test_createProjectDir_idempotent() {
    TEST("createProjectDir idempotent");

    const std::string novelPath = utils::file::joinPath(kTestDir, "novel.json");
    utils::file::writeText(novelPath, "// modified manually");

    ProjectIO::createProjectDir(kTestDir, "New Title");

    CHECK(utils::file::readText(novelPath) == "// modified manually");
    PASS();
}

void test_save_load_roundtrip() {
    TEST("Project save/load roundtrip");

    Project orig;
    orig.format_version = 3;
    orig.title = "Shadow of the Ancients";
    orig.author = "Test Author";
    orig.description = "A fantasy adventure.";
    orig.logline = "A scholar steals a relic that wants to be found.";
    orig.genre = {"fantasy", "epic"};
    orig.target_word_count = 100000;
    orig.current_word_count = 12345;
    orig.status = "in_progress";
    orig.metadata["workflow"] = "drafting";

    orig.outline.premise = "A scholar finds an ancient artifact.";
    PlotThread mainThread;
    mainThread.id = "pt-main";
    mainThread.name = "Main";
    mainThread.description = "Learn the relic's truth";
    orig.outline.plot_threads.push_back(mainThread);

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "Discovery";
    ch.order = 1;
    ch.synopsis = "The protagonist finds the relic.";
    ch.status = "drafted";
    ch.word_count = 3500;
    ch.file_path = "chapters/001-discovery.md";
    Scene sc1;
    sc1.summary = "Elena searches the library stacks.";
    Scene sc2;
    sc2.summary = "The vault chamber opens during an earthquake.";
    ch.scenes = {sc1, sc2};
    ch.pov_characters = {"elena"};
    ch.key_events = {"artifact_discovered"};
    ch.themes = {"discovery", "mystery"};
    ch.metadata["emotion_curve"] = nlohmann::json::array({"calm", "panic"});
    orig.outline.chapters.push_back(ch);

    Character hero;
    hero.id = "elena";
    hero.name = "Elena Vasquez";
    hero.role = "protagonist";
    hero.traits = {"intelligent", "brave"};
    Relationship mentor;
    mentor.target_character_id = "marcus";
    mentor.type = "mentor";
    hero.relationships = {mentor};
    hero.chapter_appearances = {"ch-001"};
    hero.metadata["secret"] = "archive-key";
    orig.characters.push_back(hero);

    Setting loc;
    loc.id = "library";
    loc.name = "Thorne Library";
    loc.category = "location";
    loc.description = "Ancient library with a sealed vault.";
    loc.metadata["founded"] = "1642";
    loc.metadata["architecture"] = "gothic";
    loc.metadata["hazards"] = nlohmann::json::array({"sealed-wing"});
    orig.settings.push_back(loc);

    WorldRule rule;
    rule.id = "relic-echo";
    rule.name = "Relic Echo";
    rule.summary = "The relic amplifies a reader's obsession.";
    rule.limitations = "Only works near old inscriptions.";
    orig.world_rules.push_back(rule);

    orig.style.tone = "atmospheric";
    orig.style.pacing = "moderate";
    orig.style.chapter_length_target = 4000;
    orig.style.metadata["forbidden_topics"] = nlohmann::json::array({"internet slang"});

    orig.path = kTestDir;
    ProjectIO::save(orig);

    Project loaded = ProjectIO::load(kTestDir);

    CHECK(loaded.format_version == 4);
    CHECK(loaded.title == "Shadow of the Ancients");
    CHECK(loaded.logline == "A scholar steals a relic that wants to be found.");
    CHECK(loaded.metadata.at("workflow") == "drafting");
    CHECK(loaded.outline.plot_threads.size() == 1);
    CHECK(loaded.outline.chapters[0].metadata.at("emotion_curve").is_array());
    CHECK(loaded.outline.chapters[0].scenes.size() == 2);
    CHECK(loaded.characters[0].metadata.at("secret") == "archive-key");
    CHECK(loaded.characters[0].relationships[0].type == "mentor");
    CHECK(loaded.settings[0].metadata.at("founded") == "1642");
    CHECK(loaded.world_rules.size() == 1);
    CHECK(loaded.world_rules[0].name == "Relic Echo");
    CHECK(loaded.style.metadata.at("forbidden_topics").is_array());

    auto savedNovelJson = ProjectIO::loadJsonFile(utils::file::joinPath(kTestDir, "novel.json"));
    CHECK(savedNovelJson.has_value());
    CHECK((*savedNovelJson)["format_version"] == 4);

    PASS();
}

void test_legacy_load_migration() {
    TEST("legacy load migration");

    cleanup();
    ProjectIO::createProjectDir(kTestDir, "Legacy Test");

    ProjectIO::saveJsonFile(utils::file::joinPath(kTestDir, "novel.json"), {
        {"format_version", 1},
        {"title", "Legacy Test"},
        {"custom_flag", true}
    });
    ProjectIO::saveJsonFile(utils::file::joinPath(kTestDir, "outline.json"), {
        {"premise", "Legacy premise"},
        {"chapters", nlohmann::json::array({
            {
                {"id", "ch-legacy"},
                {"title", "Legacy Chapter"},
                {"emotion_curve", nlohmann::json::array({"calm", "alarm"})}
            }
        })}
    });
    ProjectIO::saveJsonFile(utils::file::joinPath(kTestDir, "settings.json"), nlohmann::json::array({
        {
            {"id", "archive"},
            {"name", "Archive"},
            {"temperature", "cold"}
        }
    }));

    Project loaded = ProjectIO::load(kTestDir);

    CHECK(loaded.format_version == 4);
    CHECK(loaded.metadata.at("custom_flag") == true);
    CHECK(loaded.outline.chapters[0].metadata.at("emotion_curve").is_array());
    CHECK(loaded.settings[0].metadata.at("temperature") == "cold");

    PASS();
}

void test_chapter_read_write() {
    TEST("chapter markdown read/write");

    const std::string chapterPath = "chapters/01-test.md";
    const std::string content = "# Chapter 1\n\nThis is a test.\n";

    ProjectIO::writeChapter(kTestDir, chapterPath, content);
    CHECK(utils::file::exists(utils::file::joinPath(kTestDir, chapterPath)));
    CHECK(ProjectIO::readChapter(kTestDir, chapterPath) == content);

    PASS();
}

void test_chapter_read_missing() {
    TEST("chapter read missing");
    CHECK(ProjectIO::readChapter(kTestDir, "chapters/does-not-exist.md").empty());
    PASS();
}

void test_loadJsonFile_edge_cases() {
    TEST("loadJsonFile edge cases");

    auto missing = ProjectIO::loadJsonFile(kTestDir + "/_missing_.json");
    CHECK(!missing.has_value());

    const std::string emptyPath = utils::file::joinPath(kTestDir, "_empty.json");
    utils::file::writeText(emptyPath, "");
    CHECK(!ProjectIO::loadJsonFile(emptyPath).has_value());
    utils::file::removeFile(emptyPath);

    const std::string badPath = utils::file::joinPath(kTestDir, "_bad.json");
    utils::file::writeText(badPath, "{ bad json");
    CHECK(!ProjectIO::loadJsonFile(badPath).has_value());
    utils::file::removeFile(badPath);

    PASS();
}

void test_pm_create_open_and_validate() {
    TEST("ProjectManager create/open/validate");

    cleanup();
    ProjectManager pm;
    Project created = pm.create(kTestDir, "Manager Test");
    CHECK(created.title == "Manager Test");
    CHECK(created.path == kTestDir);
    CHECK(pm.isValid(kTestDir));

    Project opened = pm.open(kTestDir);
    CHECK(opened.title == "Manager Test");
    CHECK(opened.path == kTestDir);

    PASS();
}

// 回归测试（Issue 23 补漏）：exists() 的相对路径必须以项目根目录为基准
// 解析，而非进程当前工作目录——与 loadJson/saveJson 的路径语义保持一致。
void test_storage_backend_exists_path_semantics() {
    TEST("FileStorageBackend::exists 相对路径按项目根解析");

    cleanup();
    ProjectIO::createProjectDir(kTestDir, "Exists Test");
    FileStorageBackend backend(kTestDir);

    // 用 saveJson 写入相对路径文件（落在项目根下）
    backend.saveJson("sub/exists_probe.json", nlohmann::json{{"k", 1}});

    // 相对路径：按项目根解析应命中（CWD 下并无 sub/exists_probe.json）
    CHECK(backend.exists("sub/exists_probe.json"));
    // 已含项目路径的形式：直接判断，行为不变
    CHECK(backend.exists(utils::file::joinPath(kTestDir, "sub/exists_probe.json")));
    // 不存在的相对路径应返回 false
    CHECK(!backend.exists("sub/missing.json"));

    PASS();
}

int main() {
    std::cout << "=== test_project_io ===\n\n";

    test_createProjectDir();
    test_createProjectDir_idempotent();
    test_save_load_roundtrip();
    test_legacy_load_migration();
    test_chapter_read_write();
    test_chapter_read_missing();
    test_loadJsonFile_edge_cases();
    test_pm_create_open_and_validate();
    test_storage_backend_exists_path_semantics();

    cleanup();

    std::cout << "\nResult: " << tests_passed << '/' << tests_run << " passed\n";
    if (tests_passed == tests_run) {
        std::cout << "All tests passed!\n";
        return 0;
    }

    std::cout << (tests_run - tests_passed) << " tests failed\n";
    return 1;
}
