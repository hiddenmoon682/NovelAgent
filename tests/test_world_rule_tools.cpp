#include "agent/tools/WorldRuleTools.h"
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
        path = (fs::temp_directory_path() / "novelagent_test_rule").string();
        std::error_code ec; fs::remove_all(path, ec);
        ProjectIO::createProjectDir(path, "规则测试");
        project = ProjectIO::load(path);
        ProjectIO::save(project);
    }
    ~TestProject() { std::error_code ec; fs::remove_all(path, ec); }
};

// =========================================================================
// 测试 1: create_world_rule
// =========================================================================

void test_create_world_rule() {
    TEST("create_world_rule — 创建新世界规则");
    TestProject tp;
    agent::CreateWorldRuleTool tool(std::make_shared<ProjectAccess>(tp.project));

    auto result = tool.execute({{"name", "宵禁法"}, {"summary", "夜晚禁止外出"}});
    CHECK(result["success"] == true);
    CHECK(result["rule"]["id"] == "rule-001");

    // 验证落盘
    CHECK(tp.project.world_rules.size() == 1);
    CHECK(tp.project.world_rules[0].name == "宵禁法");
    CHECK(tp.project.world_rules[0].summary == "夜晚禁止外出");

    PASS();
}

// =========================================================================
// 测试 2: get_world_rule
// =========================================================================

void test_get_world_rule() {
    TEST("get_world_rule — 查询规则详情");
    TestProject tp;
    auto pp = std::make_shared<ProjectAccess>(tp.project);
    agent::CreateWorldRuleTool create(pp);
    create.execute({{"name", "魔法守恒"}, {"limitations", "施法需消耗生命力"}});

    agent::GetWorldRuleTool get(pp);
    auto result = get.execute({{"rule_id", "rule-001"}});
    CHECK(result["name"] == "魔法守恒");
    CHECK(result["limitations"] == "施法需消耗生命力");

    PASS();
}

// =========================================================================
// 测试 3: update_world_rule
// =========================================================================

void test_update_world_rule() {
    TEST("update_world_rule — 更新字段");
    TestProject tp;
    auto pp = std::make_shared<ProjectAccess>(tp.project);
    agent::CreateWorldRuleTool create(pp);
    create.execute({{"name", "旧规则"}});

    agent::UpdateWorldRuleTool update(pp);
    auto result = update.execute({
        {"rule_id", "rule-001"},
        {"fields", {{"name", "新规则"}, {"precedence", 5}}}
    });
    CHECK(result["success"] == true);

    CHECK(tp.project.world_rules[0].name == "新规则");
    CHECK(tp.project.world_rules[0].precedence == 5);

    PASS();
}

// =========================================================================
// 测试 4: delete_world_rule + 级联清理
// =========================================================================

void test_delete_world_rule() {
    TEST("delete_world_rule — 删除规则并级联清理引用");
    TestProject tp;
    auto pp = std::make_shared<ProjectAccess>(tp.project);
    agent::CreateWorldRuleTool create(pp);
    create.execute({{"name", "待删除规则"}});
    std::string rid = tp.project.world_rules[0].id;

    // 在 Setting 中引用该规则
    Setting s;
    s.id = "setting-001"; s.name = "测试地点";
    s.related_rule_ids.push_back(rid);
    tp.project.settings.push_back(s);

    agent::DeleteWorldRuleTool del(pp);
    auto r = del.execute({{"rule_id", rid}});
    CHECK(r.value("success", false) == true);
    CHECK(tp.project.world_rules.empty());
    // 级联：Setting 引用被清理
    CHECK(tp.project.settings[0].related_rule_ids.empty());

    PASS();
}

// =========================================================================
// 测试 5: 错误处理
// =========================================================================

void test_error_handling() {
    TEST("create/delete — 空名称/不存在");
    TestProject tp;
    auto pp = std::make_shared<ProjectAccess>(tp.project);

    agent::CreateWorldRuleTool create(pp);
    auto r = create.execute({{"name", ""}});
    CHECK(r.contains("error"));

    agent::DeleteWorldRuleTool del(pp);
    r = del.execute({{"rule_id", "rule-999"}});
    CHECK(r.contains("error"));

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_world_rule_tools ===\n\n";
    test_create_world_rule();
    test_get_world_rule();
    test_update_world_rule();
    test_delete_world_rule();
    test_error_handling();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
