#include "utils/IdUtils.h"

#include <iostream>
#include <string>
#include <vector>

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

using utils::id::findById;
using utils::id::formatSequentialId;
using utils::id::tryParseIdNumber;

// =========================================================================
// formatSequentialId — 补零边界（D1）
// =========================================================================

void should_pad_to_three_digits_when_number_below_1000() {
    TEST("formatSequentialId — 1/9/10/99/100/999 补零到 3 位");

    CHECK(formatSequentialId("ch-", 1) == "ch-001");
    CHECK(formatSequentialId("ch-", 9) == "ch-009");
    CHECK(formatSequentialId("ch-", 10) == "ch-010");
    CHECK(formatSequentialId("ch-", 99) == "ch-099");
    CHECK(formatSequentialId("ch-", 100) == "ch-100");
    CHECK(formatSequentialId("ch-", 999) == "ch-999");

    PASS();
}

void should_not_truncate_when_number_reaches_1000() {
    TEST("formatSequentialId — >=1000 不截断");

    CHECK(formatSequentialId("ch-", 1000) == "ch-1000");
    CHECK(formatSequentialId("setting-", 12345) == "setting-12345");

    PASS();
}

void should_keep_prefix_verbatim_when_formatting() {
    TEST("formatSequentialId — 前缀原样拼接（含多段前缀）");

    // dev ID 场景：前缀本身含多个分隔符
    CHECK(formatSequentialId("dev-char-001-", 5) == "dev-char-001-005");

    PASS();
}

// =========================================================================
// tryParseIdNumber — 正常/前缀不符/非数字/空串（D2）
// =========================================================================

void should_parse_number_when_id_matches_prefix() {
    TEST("tryParseIdNumber — 正常补零 ID 解析尾号");

    auto r = tryParseIdNumber("ch-005", "ch-");
    CHECK(r.has_value());
    CHECK(*r == 5);

    auto r2 = tryParseIdNumber("setting-100", "setting-");
    CHECK(r2.has_value());
    CHECK(*r2 == 100);

    // 未补零的历史格式（dev-xxx-1）也可解析
    auto r3 = tryParseIdNumber("dev-char-001-1", "dev-char-001-");
    CHECK(r3.has_value());
    CHECK(*r3 == 1);

    PASS();
}

void should_return_nullopt_when_prefix_mismatches() {
    TEST("tryParseIdNumber — 前缀不符返回 nullopt");

    CHECK(!tryParseIdNumber("char-005", "ch-").has_value()); // "cha" ≠ "ch-"，前缀不匹配
    CHECK(!tryParseIdNumber("CH-005", "ch-").has_value());  // 大小写敏感
    CHECK(!tryParseIdNumber("ch005", "ch-").has_value());   // 缺分隔符

    PASS();
}

void should_return_nullopt_when_suffix_not_numeric() {
    TEST("tryParseIdNumber — 非数字尾号返回 nullopt");

    CHECK(!tryParseIdNumber("ch-abc", "ch-").has_value());
    // WHY: 部分数字（"5x"）必须整体拒绝——std::stoi 会解析出 5，
    // 若接受会让非标准 ID 以部分值混入编号统计
    CHECK(!tryParseIdNumber("ch-5x", "ch-").has_value());
    CHECK(!tryParseIdNumber("ch--5", "ch-").has_value());   // 负号也视为非数字
    // 超出 int 范围：内部捕获 out_of_range 后返回 nullopt，不抛异常
    CHECK(!tryParseIdNumber("ch-99999999999999999999", "ch-").has_value());

    PASS();
}

void should_return_nullopt_when_id_empty_or_suffix_empty() {
    TEST("tryParseIdNumber — 空串/空尾号返回 nullopt");

    CHECK(!tryParseIdNumber("", "ch-").has_value());
    CHECK(!tryParseIdNumber("ch-", "ch-").has_value());  // 前缀匹配但尾号为空

    PASS();
}

// =========================================================================
// findById — 命中/未命中/空容器（D1）
// =========================================================================

namespace {
// 模拟含 id 成员的实体类型
struct FakeEntity {
    std::string id;
    std::string name;
};
} // namespace

void should_return_element_pointer_when_id_found() {
    TEST("findById — 命中返回容器内元素指针（非拷贝）");

    std::vector<FakeEntity> items = {
        {"ch-001", "第一章"},
        {"ch-002", "第二章"},
        {"ch-003", "第三章"},
    };

    FakeEntity* found = findById(items, "ch-002");
    CHECK(found != nullptr);
    CHECK(found->id == "ch-002");
    CHECK(found->name == "第二章");

    // 通过指针修改应反映到容器内元素——验证返回的是原元素而非拷贝
    found->name = "改名后的第二章";
    CHECK(items[1].name == "改名后的第二章");

    PASS();
}

void should_return_nullptr_when_id_not_found() {
    TEST("findById — 未命中返回 nullptr");

    std::vector<FakeEntity> items = {
        {"ch-001", "第一章"},
        {"ch-002", "第二章"},
    };

    CHECK(findById(items, "ch-999") == nullptr);
    CHECK(findById(items, "") == nullptr);

    PASS();
}

void should_return_nullptr_when_container_empty() {
    TEST("findById — 空容器返回 nullptr");

    std::vector<FakeEntity> items;
    CHECK(findById(items, "ch-001") == nullptr);

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_id_utils ===\n\n";

    should_pad_to_three_digits_when_number_below_1000();
    should_not_truncate_when_number_reaches_1000();
    should_keep_prefix_verbatim_when_formatting();
    should_parse_number_when_id_matches_prefix();
    should_return_nullopt_when_prefix_mismatches();
    should_return_nullopt_when_suffix_not_numeric();
    should_return_nullopt_when_id_empty_or_suffix_empty();
    should_return_element_pointer_when_id_found();
    should_return_nullptr_when_id_not_found();
    should_return_nullptr_when_container_empty();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
