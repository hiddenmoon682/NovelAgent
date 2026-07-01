#include "agent/tools/OutlineTools.h"
#include "agent/tools/ChapterTools.h"
#include "project/ProjectIO.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while(0)

namespace fs = std::filesystem;
using json = nlohmann::json;

struct TestProject {
    std::string path;
    Project project;
    TestProject() {
        path = (fs::temp_directory_path() / "novelagent_test_outline").string();
        std::error_code ec; fs::remove_all(path, ec);
        ProjectIO::createProjectDir(path, "大纲测试");
        project = ProjectIO::load(path);
        ProjectIO::save(project);
    }
    ~TestProject() { std::error_code ec; fs::remove_all(path, ec); }
};

// =========================================================================
// 测试 1: get_outline（空项目）
// =========================================================================

void test_get_outline_empty() {
    TEST("get_outline — 空项目返回空数组");
    TestProject tp;
    agent::GetOutlineTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({});
    CHECK(r["premise"].is_string());
    CHECK(r["volumes"].size() == 0);
    CHECK(r["plot_threads"].size() == 0);
    CHECK(r["chapters"].size() == 0);
    PASS();
}

// =========================================================================
// 测试 2: create_volume
// =========================================================================

void test_create_volume() {
    TEST("create_volume — 创建新卷");
    TestProject tp;
    agent::CreateVolumeTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({{"title", "第一卷：启程"}});
    CHECK(r["success"] == true);
    CHECK(r["volume"]["id"] == "vol-001");
    CHECK(r["volume"]["title"] == "第一卷：启程");
    CHECK(tp.project.outline.volumes.size() == 1);
    CHECK(tp.project.outline.volumes[0].title == "第一卷：启程");
    PASS();
}

// =========================================================================
// 测试 3: update_volume
// =========================================================================

void test_update_volume() {
    TEST("update_volume — 更新卷字段");
    TestProject tp;
    auto pp = std::shared_ptr<Project>(&tp.project, [](Project*){});
    agent::CreateVolumeTool create(pp);
    create.execute({{"title", "旧标题"}});

    agent::UpdateVolumeTool update(pp);
    auto r = update.execute({
        {"volume_id", "vol-001"},
        {"fields", {{"title", "新标题"}, {"theme", "成长"}}}
    });
    CHECK(r["success"] == true);
    CHECK(tp.project.outline.volumes[0].title == "新标题");
    CHECK(tp.project.outline.volumes[0].theme == "成长");
    PASS();
}

// =========================================================================
// 测试 4: create_plot_thread
// =========================================================================

void test_create_plot_thread() {
    TEST("create_plot_thread — 创建新剧情线");
    TestProject tp;
    agent::CreatePlotThreadTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({
        {"name", "主角复仇线"},
        {"type", "main"}
    });
    CHECK(r["success"] == true);
    CHECK(r["plot_thread"]["id"] == "pt-001");
    CHECK(tp.project.outline.plot_threads.size() == 1);
    CHECK(tp.project.outline.plot_threads[0].name == "主角复仇线");
    PASS();
}

// =========================================================================
// 测试 5: update_plot_thread
// =========================================================================

void test_update_plot_thread() {
    TEST("update_plot_thread — 更新剧情线字段");
    TestProject tp;
    auto pp = std::shared_ptr<Project>(&tp.project, [](Project*){});
    agent::CreatePlotThreadTool create(pp);
    create.execute({{"name", "复仇线"}, {"description", "旧描述"}});

    agent::UpdatePlotThreadTool update(pp);
    auto r = update.execute({
        {"plot_thread_id", "pt-001"},
        {"fields", {{"description", "新描述"}, {"priority", 8}}}
    });
    CHECK(r["success"] == true);
    CHECK(tp.project.outline.plot_threads[0].description == "新描述");
    CHECK(tp.project.outline.plot_threads[0].priority == 8);
    PASS();
}

// =========================================================================
// 测试 6: get_project_status
// =========================================================================

void test_get_project_status() {
    TEST("get_project_status — 返回项目概况");
    TestProject tp;
    agent::GetProjectStatusTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({});
    CHECK(r["title"] == "大纲测试");
    CHECK(r["characters_count"] == 0);
    CHECK(r["chapters_count"] == 0);
    CHECK(r.contains("status"));
    PASS();
}

// =========================================================================
// 测试 7: create_volume + create_plot_thread 空名称错误
// =========================================================================

void test_error_handling() {
    TEST("create_volume/plot_thread — 空名称错误");
    TestProject tp;
    auto pp = std::shared_ptr<Project>(&tp.project, [](Project*){});

    agent::CreateVolumeTool cv(pp);
    auto r = cv.execute({{"title", ""}});
    CHECK(r.contains("error"));

    agent::CreatePlotThreadTool cpt(pp);
    r = cpt.execute({{"name", ""}});
    CHECK(r.contains("error"));

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_outline_tools ===\n\n";
    test_get_outline_empty();
    test_create_volume();
    test_update_volume();
    test_create_plot_thread();
    test_update_plot_thread();
    test_get_project_status();
    test_error_handling();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
