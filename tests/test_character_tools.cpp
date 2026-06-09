#include "agent/tools/CharacterTools.h"
#include "agent/ToolRegistry.h"
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
        path = (fs::temp_directory_path() / "novelagent_test_char").string();
        std::error_code ec; fs::remove_all(path, ec);
        ProjectIO::createProjectDir(path, "角色测试");
        project = ProjectIO::load(path);
        ProjectIO::save(project);
    }
    ~TestProject() { std::error_code ec; fs::remove_all(path, ec); }
};

// =========================================================================
// 测试 1: create_character
// =========================================================================

void test_create_character() {
    TEST("create_character — 创建新角色");
    TestProject tp;
    agent::CreateCharacterTool tool(tp.project);

    auto result = tool.execute({{"name", "叶凡"}, {"role", "protagonist"}});
    CHECK(result["success"] == true);
    CHECK(result["character"]["id"] == "char-001");
    CHECK(result["character"]["name"] == "叶凡");
    CHECK(result["character"]["role"] == "protagonist");

    // 验证落盘
    CHECK(tp.project.characters.size() == 1);
    CHECK(tp.project.characters[0].name == "叶凡");

    PASS();
}

// =========================================================================
// 测试 2: get_character
// =========================================================================

void test_get_character() {
    TEST("get_character — 查询角色完整档案");
    TestProject tp;

    // 先创建
    agent::CreateCharacterTool create(tp.project);
    create.execute({{"name", "苏婉"}, {"role", "protagonist"}});

    // 再查询
    agent::GetCharacterTool get(tp.project);
    auto result = get.execute({{"character_id", "char-001"}});
    CHECK(result["name"] == "苏婉");
    CHECK(result["role"] == "protagonist");
    CHECK(result.contains("personality")); // 即使为空字段也应存在（from_json 默认值）

    PASS();
}

// =========================================================================
// 测试 3: get_characters
// =========================================================================

void test_get_characters() {
    TEST("get_characters — 列出所有角色摘要");
    TestProject tp;

    agent::CreateCharacterTool create(tp.project);
    create.execute({{"name", "角色A"}});
    create.execute({{"name", "角色B"}, {"role", "antagonist"}});

    agent::ListCharactersTool list(tp.project);
    auto result = list.execute({});
    CHECK(result["characters"].size() == 2);
    CHECK(result["characters"][0]["id"] == "char-001");
    CHECK(result["characters"][1]["name"] == "角色B");
    // 摘要只返回基本字段
    CHECK(result["characters"][0].size() == 4); // id, name, role, goal

    PASS();
}

// =========================================================================
// 测试 4: update_character
// =========================================================================

void test_update_character() {
    TEST("update_character — 更新指定字段");
    TestProject tp;

    agent::CreateCharacterTool create(tp.project);
    create.execute({{"name", "张三"}});

    agent::UpdateCharacterTool update(tp.project);
    auto result = update.execute({
        {"character_id", "char-001"},
        {"fields", {
            {"personality", "沉稳果断"},
            {"background", "曾是军中将领"},
            {"traits", {"brave", "loyal"}}
        }}
    });
    CHECK(result["success"] == true);

    // 验证更新
    CHECK(tp.project.characters[0].personality == "沉稳果断");
    CHECK(tp.project.characters[0].background == "曾是军中将领");
    CHECK(tp.project.characters[0].traits.size() == 2);
    CHECK(tp.project.characters[0].traits[0] == "brave");

    // 未更新的字段不受影响
    CHECK(tp.project.characters[0].name == "张三");
    CHECK(tp.project.characters[0].goal.empty());

    PASS();
}

// =========================================================================
// 测试 5: 错误处理
// =========================================================================

void test_error_handling() {
    TEST("错误处理 — 不存在角色 / 空姓名");
    TestProject tp;

    // 查询不存在的角色
    agent::GetCharacterTool get(tp.project);
    auto result = get.execute({{"character_id", "char-999"}});
    CHECK(result.contains("error"));

    // 创建空姓名角色
    agent::CreateCharacterTool create(tp.project);
    result = create.execute({{"name", ""}});
    CHECK(result.contains("error"));

    // 更新不存在角色
    agent::UpdateCharacterTool update(tp.project);
    result = update.execute({
        {"character_id", "char-999"},
        {"fields", {{"goal", "test"}}}
    });
    CHECK(result.contains("error"));

    PASS();
}

// =========================================================================
// 测试 6: 通过 ToolRegistry 注册并执行
// =========================================================================

void test_via_registry() {
    TEST("通过 ToolRegistry 注册并执行 Character 工具");
    TestProject tp;

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(
        std::make_unique<agent::GetCharacterTool>(tp.project));
    registry.registerBuiltInTool(
        std::make_unique<agent::ListCharactersTool>(tp.project));
    registry.registerBuiltInTool(
        std::make_unique<agent::CreateCharacterTool>(tp.project));

    CHECK(registry.hasTool("get_character"));
    CHECK(registry.hasTool("get_characters"));
    CHECK(registry.hasTool("create_character"));
    CHECK(registry.toolCount() == 3);

    // 通过 registry 创建
    auto result = registry.executeTool("create_character",
        {{"name", "李四"}, {"role", "supporting"}});
    CHECK(result["success"] == true);
    CHECK(result["character"]["id"] == "char-001");

    // 通过 registry 查询
    result = registry.executeTool("get_character",
        {{"character_id", "char-001"}});
    CHECK(result["name"] == "李四");

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_character_tools ===\n\n";
    test_create_character();
    test_get_character();
    test_get_characters();
    test_update_character();
    test_error_handling();
    test_via_registry();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
