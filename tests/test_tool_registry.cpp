#include "agent/ToolRegistry.h"
#include "agent/tools/BuiltInTool.h"
#include "utils/SchemaUtils.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

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

using json = nlohmann::json;

// =========================================================================
// 测试用具体工具类 — EchoTool
// =========================================================================

class EchoTool : public agent::BuiltInTool {
public:
    std::string name() const override { return "echo"; }
    std::string description() const override { return "回显输入的参数"; }

    json parameters() const override {
        return utils::schema::object({
            {"message", utils::schema::stringProp("要回显的消息")}
        }, {"message"});
    }

    json execute(const json& args) override {
        return {{"echo", args.value("message", "")}};
    }

    agent::ToolCategory category() const override {
        return agent::ToolCategory::System;
    }
};

// 测试用具体工具类 — 故意抛异常
class FailingTool : public agent::BuiltInTool {
public:
    std::string name() const override { return "failer"; }
    std::string description() const override { return "总是失败的测试工具"; }

    json parameters() const override {
        return utils::schema::object({});
    }

    json execute(const json& /*args*/) override {
        throw std::runtime_error("模拟执行失败");
    }

    agent::ToolCategory category() const override {
        return agent::ToolCategory::System;
    }
};

// =========================================================================
// 测试 1: 注册函数式工具并执行
// =========================================================================

void test_register_and_execute_functional() {
    TEST("注册函数式工具并执行");

    agent::ToolRegistry registry;
    registry.registerTool(
        "add",
        "两数相加",
        utils::schema::object({
            {"a", utils::schema::integerProp("第一个数")},
            {"b", utils::schema::integerProp("第二个数")}
        }, {"a", "b"}),
        agent::ToolCategory::System,
        [](const json& args) -> json {
            int a = args.value("a", 0);
            int b = args.value("b", 0);
            return {{"result", a + b}};
        }
    );

    CHECK(registry.toolCount() == 1);
    CHECK(registry.hasTool("add"));

    auto result = registry.executeTool("add", {{"a", 3}, {"b", 5}});
    CHECK(result["result"] == 8);

    PASS();
}

// =========================================================================
// 测试 2: 注册 BuiltInTool 子类并执行
// =========================================================================

void test_register_and_execute_builtin() {
    TEST("注册 BuiltInTool 子类并执行");

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(std::make_unique<EchoTool>());

    CHECK(registry.toolCount() == 1);
    CHECK(registry.hasTool("echo"));

    auto result = registry.executeTool("echo", {{"message", "你好世界"}});
    CHECK(result["echo"] == "你好世界");

    PASS();
}

// =========================================================================
// 测试 3: getToolDefinitions 输出正确
// =========================================================================

void test_tool_definitions() {
    TEST("getToolDefinitions 输出 JSON 格式正确");

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(std::make_unique<EchoTool>());

    auto defs = registry.getToolDefinitions();
    CHECK(defs.size() == 1);
    CHECK(defs[0].name == "echo");
    CHECK(defs[0].description == "回显输入的参数");
    CHECK(defs[0].parameters.contains("type"));
    CHECK(defs[0].parameters["type"] == "object");
    CHECK(defs[0].parameters["properties"].contains("message"));

    PASS();
}

// =========================================================================
// 测试 4: 执行不存在的工具（错误处理）
// =========================================================================

void test_unknown_tool() {
    TEST("执行不存在的工具 → 返回错误 JSON");

    agent::ToolRegistry registry;
    auto result = registry.executeTool("nonexistent", {});

    CHECK(result.contains("error"));
    std::string err = result["error"];
    CHECK(err.find("不存在") != std::string::npos);
    CHECK(result.contains("available_tools"));

    PASS();
}

// =========================================================================
// 测试 5: 工具执行抛异常（错误处理）
// =========================================================================

void test_tool_exception() {
    TEST("工具执行抛异常 → 捕获并返回错误 JSON");

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(std::make_unique<FailingTool>());

    auto result = registry.executeTool("failer", {});

    CHECK(result.contains("error"));
    std::string err = result["error"];
    CHECK(err.find("执行异常") != std::string::npos);
    CHECK(err.find("模拟执行失败") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 6: 按类别查询
// =========================================================================

void test_category_filter() {
    TEST("toolNamesByCategory 按类别过滤");

    agent::ToolRegistry registry;
    registry.registerTool(
        "sys_tool", "系统工具",
        utils::schema::object({}),
        agent::ToolCategory::System,
        [](const json&) { return json::object(); }
    );
    registry.registerTool(
        "char_tool", "角色工具",
        utils::schema::object({}),
        agent::ToolCategory::Character,
        [](const json&) { return json::object(); }
    );

    auto systemTools = registry.toolNamesByCategory(agent::ToolCategory::System);
    CHECK(systemTools.size() == 1);
    CHECK(systemTools[0] == "sys_tool");

    auto charTools = registry.toolNamesByCategory(agent::ToolCategory::Character);
    CHECK(charTools.size() == 1);
    CHECK(charTools[0] == "char_tool");

    // 空类别
    auto outlineTools = registry.toolNamesByCategory(agent::ToolCategory::Outline);
    CHECK(outlineTools.empty());

    // toolNames 返回全部
    auto all = registry.toolNames();
    CHECK(all.size() == 2);

    PASS();
}

// =========================================================================
// 测试 7: BuiltInTool::toDefinition 输出正确
// =========================================================================

void test_to_definition() {
    TEST("BuiltInTool::toDefinition 转换正确");

    EchoTool echo;
    auto def = echo.toDefinition();

    CHECK(def.name == "echo");
    CHECK(def.description == "回显输入的参数");
    CHECK(def.parameters["type"] == "object");
    CHECK(def.parameters["properties"].contains("message"));
    CHECK(def.parameters["required"].is_array());
    CHECK(def.parameters["required"][0] == "message");

    PASS();
}

// =========================================================================
// 测试 8: SchemaUtils 属性类型正确
// =========================================================================

void test_schema_utils() {
    TEST("SchemaUtils 各属性类型正确");

    auto str = utils::schema::stringProp("测试");
    CHECK(str["type"] == "string");
    CHECK(str["description"] == "测试");

    auto num = utils::schema::integerProp("数量");
    CHECK(num["type"] == "integer");

    auto flag = utils::schema::booleanProp("开关");
    CHECK(flag["type"] == "boolean");

    auto enm = utils::schema::stringEnum("方向", {"north", "south"});
    CHECK(enm["type"] == "string");
    CHECK(enm["enum"].size() == 2);
    CHECK(enm["enum"][0] == "north");

    auto arr = utils::schema::stringArrayProp("标签列表");
    CHECK(arr["type"] == "array");
    CHECK(arr["items"]["type"] == "string");

    // object 的 additionalProperties 应为 false（安全默认）
    auto obj = utils::schema::object({
        {"name", utils::schema::stringProp("名称")}
    }, {"name"});
    CHECK(obj["type"] == "object");
    CHECK(obj["additionalProperties"] == false);
    CHECK(obj["required"][0] == "name");

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_tool_registry ===\n\n";

    test_register_and_execute_functional();
    test_register_and_execute_builtin();
    test_tool_definitions();
    test_unknown_tool();
    test_tool_exception();
    test_category_filter();
    test_to_definition();
    test_schema_utils();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
