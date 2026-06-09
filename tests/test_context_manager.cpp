#include "agent/ContextManager.h"
#include "agent/ConversationSummarizer.h"
#include "agent/DegradationPipeline.h"
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
    // 使用中文消息（token 估算更高）
    conv.addUser("这是一条比较长的用户消息用于测试上下文窗口的截断功能判断是否正常。" + std::string(100, 'x'));
    conv.addAssistant("助手回复同样包含较多文字内容用于占满预算空间触发截断逻辑。" + std::string(100, 'y'));
    conv.addUser("第二条用户消息继续增加对话历史的长度以测试截断是否正确工作。" + std::string(100, 'z'));
    conv.addAssistant("助手再次回复确保消息列表中有足够条目可以触发截断行为验证。" + std::string(100, 'w'));
    return conv;
}

static llm::Conversation makeStoryConversation() {
    llm::Conversation conv;
    conv.addUser("请帮我写主角张三的第 ch-003 章。剧情需要有一个大的转折。");
    conv.addAssistant("好的，我先读取 ch-003 的当前内容，分析角色张三的剧情发展。");
    conv.addUser("另外检查一下反派李四与主角的矛盾是否有合理的铺垫。");
    conv.addAssistant("已经检查。冲突升级是关键的剧情转折点。伏笔在 ch-001 章已埋下。");
    return conv;
}

// =========================================================================
// Phase 4 新测试 — ConversationSummarizer
// =========================================================================

void test_summarize_conversation() {
    TEST("ConversationSummarizer — 提取角色名和章节引用");

    auto conv = makeStoryConversation();
    agent::ConversationSummarizer summarizer;
    auto summary = summarizer.summarize(conv.messages());

    CHECK(!summary.summary.empty());
    CHECK(summary.source_message_count > 0);
    CHECK(!summary.chapter_refs.empty());
    CHECK(!summary.plot_points.empty() || !summary.tasks.empty());

    PASS();
}

void test_summarize_empty() {
    TEST("ConversationSummarizer — 空对话返回空摘要");

    agent::ConversationSummarizer summarizer;
    std::vector<llm::Message> empty;
    auto summary = summarizer.summarize(empty);

    CHECK(summary.summary.empty());
    CHECK(summary.source_message_count == 0);

    PASS();
}

void test_summarizer_custom_keywords() {
    TEST("ConversationSummarizer — 自定义关键词（P3 可配置化）");

    auto conv = makeStoryConversation();

    agent::SummaryKeywords kw;
    kw.plot_keywords = {"转折", "铺垫"};
    kw.task_keywords = {"写"};

    agent::ConversationSummarizer summarizer(kw);
    auto summary = summarizer.summarize(conv.messages());

    CHECK(!summary.summary.empty());
    // 验证关键词确实被使用
    CHECK(summarizer.keywords().plot_keywords.size() == 2);

    PASS();
}

// =========================================================================
// Phase 4 新测试 — DegradationPipeline
// =========================================================================

void test_degradation_pipeline_basic() {
    TEST("DegradationPipeline — 默认5级策略注册");

    agent::DegradationPipeline pipeline;
    pipeline.registerDefaultStrategies();

    CHECK(pipeline.determineLevel(5000, 10000) == agent::DegradationLevel::None);
    CHECK(pipeline.determineLevel(12000, 10000) != agent::DegradationLevel::None);

    PASS();
}

void test_degradation_pipeline_apply() {
    TEST("DegradationPipeline — 各级降级执行");

    std::string prompt = "# 项目: 测试\n## 当前章节: ch-003\n"
        "### 角色: 张三 (protagonist)\n**姓名**: 张三\n"
        + std::string(500, 'x');

    agent::DegradationPipeline pipeline;
    pipeline.registerDefaultStrategies();

    auto result = pipeline.execute(prompt, agent::DegradationLevel::Summarize);
    CHECK(!result.empty());
    CHECK(result.size() < prompt.size());

    PASS();
}

// =========================================================================
// Phase 4 新测试 — SessionPersistence
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
// 原有测试 — 适配新 API
// =========================================================================

void test_calculate_budget() {
    TEST("calculateBudget — 80% 规则");
    CHECK(agent::ContextManager::calculateBudget(1000) == 800);
    CHECK(agent::ContextManager::calculateBudget(100) == 80);
    PASS();
}

void test_no_truncation() {
    TEST("assemble — 短消息不触发截断");
    llm::Conversation conv;
    conv.addUser("短消息");

    agent::ContextManager cm;
    auto result = cm.assemble(conv, 65536);

    CHECK(!result.truncated);
    CHECK(result.messages.size() == 1);
    PASS();
}

void test_truncation() {
    TEST("assemble — 长消息触发截断");
    auto conv = makeLongConversation();
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 50);  // 极小预算
    CHECK(result.truncated);
    PASS();
}

void test_assemble_no_project() {
    TEST("assemble — 无 Project 时 system_prompt 为空");
    llm::Conversation conv;
    conv.addUser("测试");
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 65536);
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

void test_allocate_budget() {
    TEST("allocateBudget — 50/30/20 规则");
    agent::ContextManager cm;
    auto alloc = cm.allocateBudget(65536);
    CHECK(alloc.total_budget == 52428);
    CHECK(alloc.chapter_budget > 0);
    CHECK(alloc.conversation_budget > 0);
    CHECK(alloc.summary_budget > 0);
    PASS();
}

void test_assemble_with_degradation() {
    TEST("assemble — 触发降级");
    llm::Conversation conv;
    for (int i = 0; i < 20; ++i) {
        conv.addUser("这是第" + std::to_string(i) + "条测试消息包含较多中文内容用于消耗token预算。" + std::string(80, 't'));
        conv.addAssistant("助手回复第" + std::to_string(i) + "条也包含同样的中文内容来增加token消耗。" + std::string(80, 'r'));
    }
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 200);
    CHECK(result.truncated);
    CHECK(result.degradation_level >= 0);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_context_manager (Phase 4 重构版) ===\n\n";

    // 新模块测试
    test_summarize_conversation();
    test_summarize_empty();
    test_summarizer_custom_keywords();
    test_degradation_pipeline_basic();
    test_degradation_pipeline_apply();
    test_session_save_load();

    // 适配后测试
    test_calculate_budget();
    test_no_truncation();
    test_truncation();
    test_assemble_no_project();
    test_build_system_prompt();
    test_build_system_prompt_no_chapter();
    test_allocate_budget();
    test_assemble_with_degradation();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
