#include "agent/tools/ChapterTools.h"
#include "agent/ToolRegistry.h"
#include "project/ProjectIO.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

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

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── 辅助：创建测试项目 ──

struct TestProject {
    std::string path;
    Project project;

    TestProject() {
        // 使用临时目录
        path = (fs::temp_directory_path() / "novelagent_test_3_4").string();
        // 清理旧目录（如果存在）
        std::error_code ec;
        fs::remove_all(path, ec);

        ProjectIO::createProjectDir(path, "测试小说");

        // 加载项目
        project = ProjectIO::load(path);

        // 添加一个初始章节
        Chapter ch;
        ch.id = "ch-001";
        ch.title = "第一章";
        ch.order = 1;
        ch.file_path = "chapters/ch-001-chapter1.md";
        ch.synopsis = "初始章节";
        project.outline.chapters.push_back(ch);
        ProjectIO::save(project);

        // 写入初始章节内容
        ProjectIO::writeChapter(path, ch.file_path, "# 第一章\n\n这是第一章的内容。\n");
    }

    ~TestProject() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// =========================================================================
// 测试 1: list_chapters
// =========================================================================

void test_list_chapters() {
    TEST("list_chapters — 列出所有章节");

    TestProject tp;
    agent::ListChaptersTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({});
    CHECK(result.contains("chapters"));
    CHECK(result["chapters"].is_array());
    CHECK(result["chapters"].size() == 1);
    CHECK(result["chapters"][0]["id"] == "ch-001");
    CHECK(result["chapters"][0]["title"] == "第一章");

    PASS();
}

// =========================================================================
// 测试 2: read_chapter
// =========================================================================

void test_read_chapter() {
    TEST("read_chapter — 读取章节全文");

    TestProject tp;
    agent::ReadChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({{"chapter_id", "ch-001"}});
    CHECK(result.contains("content"));
    CHECK(result["title"] == "第一章");
    CHECK(result["content"].get<std::string>().find("第一章的内容") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 3: write_chapter
// =========================================================================

void test_write_chapter() {
    TEST("write_chapter — 覆写章节内容");

    TestProject tp;
    agent::WriteChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({
        {"chapter_id", "ch-001"},
        {"content", "# 改写后的第一章\n\n新内容。\n"}
    });
    CHECK(result["success"] == true);

    // 验证写入
    agent::ReadChapterTool reader(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto readback = reader.execute({{"chapter_id", "ch-001"}});
    CHECK(readback["content"].get<std::string>().find("改写后") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 4: append_to_chapter
// =========================================================================

void test_append_to_chapter() {
    TEST("append_to_chapter — 追加内容");

    TestProject tp;
    agent::AppendChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({
        {"chapter_id", "ch-001"},
        {"content", "\n## 新段落\n\n追加的内容。\n"}
    });
    CHECK(result["success"] == true);

    // 验证原始内容仍在 + 新内容已追加
    agent::ReadChapterTool reader(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto readback = reader.execute({{"chapter_id", "ch-001"}});
    std::string content = readback["content"];
    CHECK(content.find("第一章的内容") != std::string::npos); // 原有内容
    CHECK(content.find("追加的内容") != std::string::npos);   // 新内容

    PASS();
}

// =========================================================================
// 测试 5: create_chapter
// =========================================================================

void test_create_chapter() {
    TEST("create_chapter — 创建新章节");

    TestProject tp;
    agent::CreateChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({{"title", "第二章"}, {"synopsis", "第二章节"}});
    CHECK(result["success"] == true);
    CHECK(result["chapter"]["id"] == "ch-002");
    CHECK(result["chapter"]["title"] == "第二章");

    // 验证 outline 已更新
    CHECK(tp.project.outline.chapters.size() == 2);

    // 验证文件已创建
    std::string file_path = result["chapter"]["file_path"];
    std::string content = ProjectIO::readChapter(tp.path, file_path);
    CHECK(!content.empty());
    CHECK(content.find("第二章") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 6: read_chapter 不存在的章节
// =========================================================================

void test_read_nonexistent() {
    TEST("read_chapter — 不存在的章节返回错误");

    TestProject tp;
    agent::ReadChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({{"chapter_id", "ch-999"}});
    CHECK(result.contains("error"));
    CHECK(result["error"].get<std::string>().find("不存在") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 7: 通过 ToolRegistry 注册并执行
// =========================================================================

void test_via_registry() {
    TEST("通过 ToolRegistry 注册并执行 Chapter 工具");

    TestProject tp;
    agent::ToolRegistry registry;

    registry.registerBuiltInTool(
        std::make_unique<agent::ListChaptersTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::ReadChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));

    CHECK(registry.hasTool("list_chapters"));
    CHECK(registry.hasTool("read_chapter"));

    auto defs = registry.getToolDefinitions();
    CHECK(defs.size() == 2);

    // 执行
    auto result = registry.executeTool("list_chapters", {});
    CHECK(result.contains("chapters"));
    CHECK(result["chapters"].size() == 1);

    PASS();
}

// ============================================================================
// A8 + A7 新工具测试
// ============================================================================

void test_delete_chapter() {
    TEST("delete_chapter — 删除章节并验证级联清理");
    TestProject tp;
    agent::CreateChapterTool create(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    create.execute(json{
        {"title", "待删除章"}, {"order", 2},
        {"goal", "测试"}, {"conflict", "测试"}
    });
    // TestProject 自带 ch-001，create 后 chapter 数 = 2
    CHECK(tp.project.outline.chapters.size() == 2);
    std::string ch_id = tp.project.outline.chapters.back().id;
    PlotThread pt;
    pt.id = "pt-test"; pt.name = "测试线"; pt.start_chapter_id = ch_id;
    tp.project.outline.plot_threads.push_back(pt);
    agent::DeleteChapterTool del(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = del.execute(json{{"chapter_id", ch_id}});
    CHECK(r.value("success", false) == true);
    CHECK(tp.project.outline.chapters.size() == 1);  // 回到只有 ch-001
    CHECK(tp.project.outline.plot_threads[0].start_chapter_id.empty());
    PASS();
}

void test_update_chapter_scenes() {
    TEST("update_chapter_scenes — 完整替换场景列表");
    TestProject tp;
    agent::CreateChapterTool create(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    create.execute(json{{"title", "场景章"}, {"order", 1}});
    std::string ch_id = tp.project.outline.chapters[0].id;
    agent::UpdateChapterScenesTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    json scenes = json::array();
    scenes.push_back(json{
        {"id", "sc-001"}, {"title", "开场"}, {"goal", "引入主角"},
        {"conflict", "内心挣扎"}, {"outcome", "做出决定"}
    });
    auto r = tool.execute(json{{"chapter_id", ch_id}, {"scenes", scenes}});
    CHECK(r.value("success", false) == true);
    CHECK(tp.project.outline.chapters[0].scenes[0].goal == "引入主角");
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_chapter_tools ===\n\n";

    test_list_chapters();
    test_read_chapter();
    test_write_chapter();
    test_append_to_chapter();
    test_create_chapter();
    test_read_nonexistent();
    test_via_registry();
    test_delete_chapter();
    test_update_chapter_scenes();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
