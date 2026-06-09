#include "agent/ContextManager.h"
#include "llm/Conversation.h"
#include "llm/TokenCounter.h"
#include "project/Models.h"

#include <cassert>
#include <iostream>
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

// =========================================================================
// 辅助: 创建长对话（用于测试截断）
// =========================================================================
static llm::Conversation makeLongConversation() {
    llm::Conversation conv;
    // 每条消息 ~50 tokens（中文约 65 字）
    conv.addUser("这是一条比较长的用户消息用于测试上下文窗口的截断功能。" + std::string(30, 'x'));
    conv.addAssistant("这是助手的回复消息同样包含较多的文字内容用于占满预算空间。" + std::string(30, 'y'));
    conv.addUser("第二条用户消息继续增加对话历史的长度以便测试截断逻辑是否正常工作。" + std::string(30, 'z'));
    conv.addAssistant("助手再次回复确保消息列表中有足够多的条目可以触发截断行为。" + std::string(30, 'w'));
    return conv;
}

// =========================================================================
// 测试 1: calculateBudget
// =========================================================================

void test_calculate_budget() {
    TEST("calculateBudget — 80% 输出预留规则");

    CHECK(agent::ContextManager::calculateBudget(1000) == 800);
    CHECK(agent::ContextManager::calculateBudget(65536) == 52428);
    CHECK(agent::ContextManager::calculateBudget(100) == 80);

    PASS();
}

// =========================================================================
// 测试 2: 截断不触发 — 消息在预算内
// =========================================================================

void test_no_truncation() {
    TEST("截断不触发 — 消息 token 在预算内");

    llm::Conversation conv;
    conv.addUser("短消息");

    agent::ContextManager cm;
    auto result = cm.assemble(conv, 65536);

    CHECK(!result.truncated);
    CHECK(result.truncated_count == 0);
    CHECK(result.messages.size() == 1);
    CHECK(result.messages[0].content == "短消息");
    CHECK(result.total_tokens > 0);
    CHECK(result.budget > 0);

    PASS();
}

// =========================================================================
// 测试 3: 截断触发 — 消息超出预算
// =========================================================================

void test_truncation() {
    TEST("截断触发 — 旧消息被移除");

    auto conv = makeLongConversation();
    int original_size = static_cast<int>(conv.size());

    agent::ContextManager cm;
    // 设置极小的上下文窗口 → 预算严重不足 → 触发截断
    auto result = cm.assemble(conv, 100);

    CHECK(result.truncated);
    CHECK(result.truncated_count > 0);
    CHECK(static_cast<int>(result.messages.size()) < original_size);

    // 最后一条消息应该被保留（用户最新的输入）
    CHECK(result.messages.back().content.find("第二条") != std::string::npos
          || result.messages.back().role == llm::MessageRole::Assistant);

    PASS();
}

// =========================================================================
// 测试 4: assemble 无 Project — system_prompt 为空
// =========================================================================

void test_assemble_no_project() {
    TEST("assemble 无 Project — system_prompt 为空串");

    llm::Conversation conv;
    conv.addUser("测试消息");

    agent::ContextManager cm;
    auto result = cm.assemble(conv, 65536);

    CHECK(result.system_prompt.empty());
    CHECK(result.messages.size() == 1);
    CHECK(!result.truncated);

    PASS();
}

// =========================================================================
// 测试 5: buildSystemPrompt 生成非空提示词
// =========================================================================

void test_build_system_prompt() {
    TEST("buildSystemPrompt — 生成有效提示词");

    // 构造一个最简 Project
    Project project;
    project.title = "测试小说";
    project.logline = "这是一个测试项目的 logline。";
    project.theme = "成长与救赎";
    project.format_version = 4;

    // 添加一个章节作为目标
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    ch.order = 1;
    project.outline.chapters.push_back(ch);

    agent::ContextManager cm;
    auto prompt = cm.buildSystemPrompt(project, "ch-001");

    CHECK(!prompt.empty());
    // 提示词应包含项目标题
    CHECK(prompt.find("测试小说") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 6: buildSystemPrompt 无章节 → 最小化提示词
// =========================================================================

void test_build_system_prompt_no_chapter() {
    TEST("buildSystemPrompt 无章节 → 仅项目概述");

    Project project;
    project.title = "极简项目";

    agent::ContextManager cm;
    auto prompt = cm.buildSystemPrompt(project);

    CHECK(!prompt.empty());
    CHECK(prompt.find("极简项目") != std::string::npos);
    CHECK(prompt.find("# 项目") != std::string::npos);

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_context_manager ===\n\n";

    test_calculate_budget();
    test_no_truncation();
    test_truncation();
    test_assemble_no_project();
    test_build_system_prompt();
    test_build_system_prompt_no_chapter();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
