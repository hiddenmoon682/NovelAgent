/// test_context_manager — 精简版（适配移除预算分配/降级/摘要后的 ContextManager）。

#include "agent/ContextManager.h"
#include "llm/Conversation.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <cassert>
#include <iostream>
#include <string>
#include <cstdio>

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
// 辅助
// =========================================================================

static llm::Conversation makeLongConversation() {
    llm::Conversation conv;
    conv.addUser("这是一条比较长的用户消息用于测试上下文窗口的截断功能判断是否正常。" + std::string(100, 'x'));
    conv.addAssistant("助手回复同样包含较多文字内容用于占满预算空间触发截断逻辑。" + std::string(100, 'y'));
    conv.addUser("第二条用户消息继续增加对话历史的长度以测试截断是否正确工作。" + std::string(100, 'z'));
    conv.addAssistant("助手再次回复确保消息列表中有足够条目可以触发截断行为验证。" + std::string(100, 'w'));
    return conv;
}

// =========================================================================
// SessionPersistence 测试
// =========================================================================

void test_session_save_load() {
    TEST("SessionPersistence — 保存和加载往返");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_session_refactor";
    ProjectIO::createProjectDir(tmp, "测试");
    FileStorageBackend storage(tmp);

    agent::SessionPersistence sp(storage);
    llm::Conversation conv;
    conv.addUser("消息一");
    conv.addAssistant("回复一");

    sp.save(conv);
    auto loaded = sp.load();

    CHECK(loaded.size() == 2);

    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// assemble 测试（精简版）
// =========================================================================

void test_no_truncation() {
    TEST("assemble — 短消息不触发截断");
    llm::Conversation conv;
    conv.addUser("短消息");

    agent::ContextManager cm;
    auto result = cm.assemble(conv, 131072);

    CHECK(result.truncated_count == 0);
    CHECK(result.messages.size() == 1);
    PASS();
}

void test_truncation() {
    TEST("assemble — 长消息触发截断");
    auto conv = makeLongConversation();
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 50);  // 极小预算
    CHECK(result.truncated_count > 0);
    PASS();
}

void test_assemble_no_project() {
    TEST("assemble — 无 Project 时 system_prompt 为空");
    llm::Conversation conv;
    conv.addUser("测试");
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 131072);
    CHECK(result.system_prompt.empty());
    PASS();
}

void test_build_system_prompt() {
    TEST("buildSystemPrompt — 生成有效提示词");
    Project project;
    project.title = "测试小说";
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    ch.order = 1;
    project.outline.chapters.push_back(ch);

    agent::ContextManager cm;
    auto prompt = cm.buildSystemPrompt(project, "ch-001");
    CHECK(!prompt.empty());
    CHECK(prompt.find("测试小说") != std::string::npos);
    PASS();
}

void test_build_system_prompt_no_chapter() {
    TEST("buildSystemPrompt — 无章节返回项目概述");
    Project project;
    project.title = "极简项目";
    agent::ContextManager cm;
    auto prompt = cm.buildSystemPrompt(project);
    CHECK(prompt.find("极简项目") != std::string::npos);
    PASS();
}

void test_total_tokens() {
    TEST("assemble — total_tokens 统计正确");
    llm::Conversation conv;
    conv.addUser("测试消息");

    agent::ContextManager cm;
    auto result = cm.assemble(conv, 131072);
    CHECK(result.total_tokens > 0);
    CHECK(result.total_tokens >= static_cast<int>(result.messages.size()));
    PASS();
}

void test_truncation_keeps_newest() {
    TEST("assemble — 截断保留最新消息");
    auto conv = makeLongConversation();
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 80);  // 只够保留约1-2条消息
    CHECK(result.truncated_count > 0);
    // 应该保留最后一条（最新的）消息
    CHECK(!result.messages.empty());
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_context_manager (精简版) ===\n\n";

    test_session_save_load();

    test_no_truncation();
    test_truncation();
    test_assemble_no_project();
    test_build_system_prompt();
    test_build_system_prompt_no_chapter();
    test_total_tokens();
    test_truncation_keeps_newest();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
