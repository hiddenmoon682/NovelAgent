// ParameterValidator 测试（C4: additionalProperties 阻断）。
// 验证 schema 设 additionalProperties:false 时，未知字段被阻断（而非仅 warn），
// 使 LLM 拼错字段名（如 charcter_id）能被自纠，不再静默吞掉浪费一轮工具调用。

#include "agent/tool/ParameterValidator.h"
#include "utils/SchemaUtils.h"

#include <iostream>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while(0)

using json = nlohmann::json;

// =========================================================================
// 测试 1: additionalProperties:false 时未知字段被阻断（C4 核心）
// =========================================================================

void test_extra_field_blocked() {
    TEST("C4: additionalProperties:false 时未知字段阻断");
    auto schema = utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content",    utils::schema::stringProp("章节内容")}
    }, {"chapter_id", "content"});  // 默认 additionalProperties=false

    // 拼错字段名 charcter_id（应为 chapter_id）
    json args = {{"charcter_id", "ch-001"}, {"content", "x"}};
    auto r = agent::ParameterValidator::validate(schema, args);
    CHECK(!r.valid);
    CHECK(!r.errors.empty());
    // 错误信息应指明未知字段
    bool mentions_unknown = false;
    for (const auto& e : r.errors) {
        if (e.field == "charcter_id") { mentions_unknown = true; break; }
    }
    CHECK(mentions_unknown);
    PASS();
}

// =========================================================================
// 测试 2: 仅合法字段时放行
// =========================================================================

void test_only_known_fields_pass() {
    TEST("C4: 仅合法字段放行");
    auto schema = utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content",    utils::schema::stringProp("章节内容")}
    }, {"chapter_id", "content"});

    json args = {{"chapter_id", "ch-001"}, {"content", "正文"}};
    auto r = agent::ParameterValidator::validate(schema, args);
    CHECK(r.valid);
    CHECK(r.errors.empty());
    PASS();
}

// =========================================================================
// 测试 3: additionalProperties:true 时未知字段放行（C5 fields 故意开放场景）
// =========================================================================

void test_extra_field_allowed_when_opt_in() {
    TEST("C4: additionalProperties:true 时未知字段放行");
    // 模拟 update_* 的 fields 故意开放（虽然 C5 已改为列字段，但 allowExtra 路径仍需正确）
    auto schema = utils::schema::object({
        {"fields", utils::schema::object({}, {}, /*allowExtra=*/true)}
    }, {"fields"});

    json args = {{"fields", {{"any_field", "x"}, {"another", "y"}}}};
    auto r = agent::ParameterValidator::validate(schema, args);
    CHECK(r.valid);
    PASS();
}

// =========================================================================
// 测试 4: 缺少必填字段阻断
// =========================================================================

void test_missing_required_blocked() {
    TEST("缺少必填字段阻断");
    auto schema = utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content",    utils::schema::stringProp("章节内容")}
    }, {"chapter_id", "content"});

    json args = {{"chapter_id", "ch-001"}};  // 缺 content
    auto r = agent::ParameterValidator::validate(schema, args);
    CHECK(!r.valid);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_parameter_validator ===\n\n";
    test_extra_field_blocked();
    test_only_known_fields_pass();
    test_extra_field_allowed_when_opt_in();
    test_missing_required_blocked();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
