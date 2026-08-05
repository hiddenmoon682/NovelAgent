#include "agent/tools/SettingTools.h"
#include "project/ProjectAccess.h"
#include "agent/tool/ToolRegistry.h"
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
        path = (fs::temp_directory_path() / "novelagent_test_setting").string();
        std::error_code ec; fs::remove_all(path, ec);
        ProjectIO::createProjectDir(path, "设定测试");
        project = ProjectIO::load(path);
        ProjectIO::save(project);
    }
    ~TestProject() { std::error_code ec; fs::remove_all(path, ec); }
};

// =========================================================================
// 测试 1: create_setting
// =========================================================================

void test_create_setting() {
    TEST("create_setting — 创建新设定");
    TestProject tp;
    agent::CreateSettingTool tool(std::make_shared<ProjectAccess>(tp.project));

    auto result = tool.execute({{"name", "废弃密室"}, {"category", "location"}});
    CHECK(result["success"] == true);
    CHECK(result["setting"]["id"] == "setting-001");
    CHECK(result["setting"]["name"] == "废弃密室");

    // 验证落盘
    CHECK(tp.project.settings.size() == 1);
    CHECK(tp.project.settings[0].name == "废弃密室");

    PASS();
}

// =========================================================================
// 测试 2: get_setting
// =========================================================================

void test_get_setting() {
    TEST("get_setting — 查询设定详情");
    TestProject tp;
    agent::CreateSettingTool create(std::make_shared<ProjectAccess>(tp.project));
    create.execute({{"name", "古堡"}, {"description", "废弃的哥特式古堡"}});

    agent::GetSettingTool get(std::make_shared<ProjectAccess>(tp.project));
    auto result = get.execute({{"setting_id", "setting-001"}});
    CHECK(result["name"] == "古堡");
    CHECK(result["description"] == "废弃的哥特式古堡");
    CHECK(result.contains("category"));

    PASS();
}

// =========================================================================
// 测试 3: get_settings
// =========================================================================

void test_get_settings() {
    TEST("get_settings — 列出所有设定摘要");
    TestProject tp;
    agent::CreateSettingTool create(std::make_shared<ProjectAccess>(tp.project));
    create.execute({{"name", "城市广场"}});
    create.execute({{"name", "地下暗道"}});

    agent::ListSettingsTool list(std::make_shared<ProjectAccess>(tp.project));
    auto result = list.execute({});
    CHECK(result["settings"].size() == 2);
    CHECK(result["settings"][0]["name"] == "城市广场");
    CHECK(result["settings"][1]["name"] == "地下暗道");

    PASS();
}

// =========================================================================
// 测试 4: update_setting
// =========================================================================

void test_update_setting() {
    TEST("update_setting — 更新字段");
    TestProject tp;
    agent::CreateSettingTool create(std::make_shared<ProjectAccess>(tp.project));
    create.execute({{"name", "旧址"}});

    agent::UpdateSettingTool update(std::make_shared<ProjectAccess>(tp.project));
    auto result = update.execute({
        {"setting_id", "setting-001"},
        {"fields", {{"description", "一片废墟"}, {"story_function", "主角藏身处"}}}
    });
    CHECK(result["success"] == true);

    CHECK(tp.project.settings[0].description == "一片废墟");
    CHECK(tp.project.settings[0].story_function == "主角藏身处");
    CHECK(tp.project.settings[0].name == "旧址");  // 不更新的字段不变

    PASS();
}

// =========================================================================
// 测试 5: delete_setting + 级联清理
// =========================================================================

void test_delete_setting() {
    TEST("delete_setting — 删除设定并级联清理引用");
    TestProject tp;
    agent::CreateSettingTool create(std::make_shared<ProjectAccess>(tp.project));
    create.execute({{"name", "待删除设定"}});
    CHECK(tp.project.settings.size() == 1);
    std::string sid = tp.project.settings[0].id;

    // 在 PlotThread 中引用该设定
    PlotThread pt;
    pt.id = "pt-001"; pt.name = "测试线";
    pt.related_settings.push_back(sid);
    tp.project.outline.plot_threads.push_back(pt);

    agent::DeleteSettingTool del(std::make_shared<ProjectAccess>(tp.project));
    auto r = del.execute({{"setting_id", sid}});
    CHECK(r.value("success", false) == true);
    CHECK(tp.project.settings.empty());
    // 级联：PlotThread 中的引用被清理
    CHECK(tp.project.outline.plot_threads[0].related_settings.empty());

    PASS();
}

// =========================================================================
// 测试 6: 不存在/空名称错误处理
// =========================================================================

void test_error_handling() {
    TEST("create_setting — 空名称 / 不存在设定");
    TestProject tp;

    agent::CreateSettingTool create(std::make_shared<ProjectAccess>(tp.project));
    auto r = create.execute({{"name", ""}});
    CHECK(r.contains("error"));

    agent::GetSettingTool get(std::make_shared<ProjectAccess>(tp.project));
    r = get.execute({{"setting_id", "setting-999"}});
    CHECK(r.contains("error"));

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_setting_tools ===\n\n";
    test_create_setting();
    test_get_setting();
    test_get_settings();
    test_update_setting();
    test_delete_setting();
    test_error_handling();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
