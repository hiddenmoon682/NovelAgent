// RulesProvider 单元测试。
// 验证「全局规则 + 项目规则」双层 Markdown 叠加语义：
//   - 双层均存在 → 含两个 H2 标题且全局在前
//   - 仅单层存在 → 只包裹对应 H2 标题
//   - 皆不存在 → 返回空串
//   - project_path 为空 → 跳过项目层只读全局
//   - 路径不存在时容错不抛异常
// 全程使用临时 config 目录，不污染真实 ~/.novelagent。

#include "agent/prompt/RulesProvider.h"
#include "utils/FileUtils.h"

#include <iostream>
#include <string>

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

const std::string kTestDir = "__test_rules_provider_tmp";
const std::string kConfigDir = utils::file::joinPath(kTestDir, "config");
const std::string kProjectDir = utils::file::joinPath(kTestDir, "proj");

void cleanup() {
    if (utils::file::exists(kTestDir)) {
        utils::file::removeDir(kTestDir);
    }
}

// 写入全局规则文件 <config>/rules.md。
void writeGlobalRules(const std::string& content) {
    utils::file::writeText(utils::file::joinPath(kConfigDir, "rules.md"), content);
}

// 写入项目规则文件 <proj>/.novelagent/rules.md。
void writeProjectRules(const std::string& content) {
    utils::file::writeText(
        utils::file::joinPath(kProjectDir, ".novelagent/rules.md"), content);
}

void test_bothLayersPresent() {
    TEST("全局 + 项目均存在：含两个 H2 且全局在前");
    cleanup();
    writeGlobalRules("统一使用第三人称叙事。");
    writeProjectRules("本项目禁用倒叙。");
    agent::prompt::RulesProvider provider(kConfigDir);
    const std::string combined = provider.combined(kProjectDir);
    const auto global_pos = combined.find("## 全局规则");
    const auto project_pos = combined.find("## 项目规则");
    CHECK(global_pos != std::string::npos);
    CHECK(project_pos != std::string::npos);
    CHECK(global_pos < project_pos);
    CHECK(combined.find("统一使用第三人称叙事。") != std::string::npos);
    CHECK(combined.find("本项目禁用倒叙。") != std::string::npos);
    PASS();
}

void test_globalOnly() {
    TEST("仅全局存在：只含「## 全局规则」");
    cleanup();
    writeGlobalRules("统一使用第三人称叙事。");
    agent::prompt::RulesProvider provider(kConfigDir);
    const std::string combined = provider.combined(kProjectDir);
    CHECK(combined.find("## 全局规则") != std::string::npos);
    CHECK(combined.find("## 项目规则") == std::string::npos);
    CHECK(combined.find("统一使用第三人称叙事。") != std::string::npos);
    PASS();
}

void test_projectOnly() {
    TEST("仅项目存在：只含「## 项目规则」");
    cleanup();
    writeProjectRules("本项目禁用倒叙。");
    agent::prompt::RulesProvider provider(kConfigDir);
    const std::string combined = provider.combined(kProjectDir);
    CHECK(combined.find("## 全局规则") == std::string::npos);
    CHECK(combined.find("## 项目规则") != std::string::npos);
    CHECK(combined.find("本项目禁用倒叙。") != std::string::npos);
    PASS();
}

void test_bothMissing() {
    TEST("皆不存在：返回空串");
    cleanup();
    utils::file::createDirs(kConfigDir);
    utils::file::createDirs(kProjectDir);
    agent::prompt::RulesProvider provider(kConfigDir);
    CHECK(provider.combined(kProjectDir).empty());
    PASS();
}

void test_emptyProjectPath() {
    TEST("project_path 为空：跳过项目层只读全局");
    cleanup();
    writeGlobalRules("统一使用第三人称叙事。");
    writeProjectRules("本项目禁用倒叙。");  // 存在但不应被读取
    agent::prompt::RulesProvider provider(kConfigDir);
    const std::string combined = provider.combined("");
    CHECK(combined.find("## 全局规则") != std::string::npos);
    CHECK(combined.find("## 项目规则") == std::string::npos);
    CHECK(combined.find("本项目禁用倒叙。") == std::string::npos);
    PASS();
}

void test_nonexistentPathsNoThrow() {
    TEST("路径不存在：容错不抛异常");
    cleanup();
    try {
        agent::prompt::RulesProvider provider("__no_such_config_dir__");
        CHECK(provider.combined("__no_such_project_dir__").empty());
    } catch (...) {
        FAIL("不应抛出异常");
    }
    PASS();
}

int main() {
    std::cout << "=== test_rules_provider (规则层叠加) ===\n\n";

    test_bothLayersPresent();
    test_globalOnly();
    test_projectOnly();
    test_bothMissing();
    test_emptyProjectPath();
    test_nonexistentPathsNoThrow();

    cleanup();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return tests_passed == tests_run ? 0 : 1;
}
