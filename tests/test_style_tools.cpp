#include "agent/tools/StyleTools.h"
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
        path = (fs::temp_directory_path() / "novelagent_test_style").string();
        std::error_code ec; fs::remove_all(path, ec);
        ProjectIO::createProjectDir(path, "风格测试");
        project = ProjectIO::load(path);
        ProjectIO::save(project);
    }
    ~TestProject() { std::error_code ec; fs::remove_all(path, ec); }
};

// =========================================================================
// 测试 1: read_style — 默认值
// =========================================================================

void test_read_style_default() {
    TEST("read_style — 默认风格配置");
    TestProject tp;
    agent::ReadStyleTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({});
    CHECK(r.contains("tone"));
    CHECK(r.contains("pov"));
    CHECK(r.contains("prose_style"));
    CHECK(r["forbidden_phrases"].is_array());
    CHECK(r["forbidden_tropes"].is_array());
    PASS();
}

// =========================================================================
// 测试 2: update_style — 字符串字段
// =========================================================================

void test_update_style_string() {
    TEST("update_style — 更新字符串字段");
    TestProject tp;
    agent::UpdateStyleTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({
        {"fields", {
            {"tone", "黑暗史诗"},
            {"pov", "第三人称有限"},
        }}
    });
    CHECK(r["success"] == true);
    CHECK(tp.project.style.tone == "黑暗史诗");
    CHECK(tp.project.style.pov == "第三人称有限");
    PASS();
}

// =========================================================================
// 测试 3: update_style — 整数字段
// =========================================================================

void test_update_style_int() {
    TEST("update_style — 更新整数字段");
    TestProject tp;
    agent::UpdateStyleTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({
        {"fields", {{"chapter_length_target", 5000}}}
    });
    CHECK(r["success"] == true);
    CHECK(tp.project.style.chapter_length_target == 5000);
    PASS();
}

// =========================================================================
// 测试 4: update_style — 数组字段
// =========================================================================

void test_update_style_array() {
    TEST("update_style — 更新数组字段");
    TestProject tp;
    agent::UpdateStyleTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({
        {"fields", {
            {"forbidden_phrases", {"突然", "毫无征兆地"}},
            {"forbidden_tropes", {"梦境开局", "失忆梗"}}
        }}
    });
    CHECK(r["success"] == true);
    CHECK(tp.project.style.forbidden_phrases.size() == 2);
    CHECK(tp.project.style.forbidden_phrases[0] == "突然");
    CHECK(tp.project.style.forbidden_tropes[1] == "失忆梗");
    PASS();
}

// =========================================================================
// 测试 5: update_style — fields 为空失败
// =========================================================================

void test_update_style_empty_fields() {
    TEST("update_style — 空 fields 返回错误");
    TestProject tp;
    agent::UpdateStyleTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = tool.execute({{"fields", {}}});
    CHECK(r.contains("error"));
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_style_tools ===\n\n";
    test_read_style_default();
    test_update_style_string();
    test_update_style_int();
    test_update_style_array();
    test_update_style_empty_fields();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
