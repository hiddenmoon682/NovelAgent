// test_context_manager — 测试新拆分的上下文组件（Compactor + ContextAssembler + TokenBudget + SessionPersistence）。

#include "agent/context/Compactor.h"
#include "agent/context/ContextAssembler.h"
#include "agent/context/TokenBudget.h"
#include "agent/context/Memory.h"
#include "agent/session/SessionPersistence.h"
#include "llm/ILLMClient.h"
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

class CompactMockLLMClient : public llm::ILLMClient {
public:
    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks,
                          const std::atomic<bool>*) override {
        return makeSummary();
    }
    llm::LLMResponse chatNonStreaming(const std::vector<llm::Message>&,
                                       const std::vector<llm::ToolDefinition>&,
                                       const std::string&) override {
        return makeSummary();
    }
    const ProviderConfig& config() const override { static ProviderConfig c; return c; }
private:
    static llm::LLMResponse makeSummary() {
        llm::LLMResponse r;
        r.content = "【压缩摘要】主角决定隐藏身份，与导师发生冲突。";
        r.finish_reason = "stop";
        return r;
    }
};

// =========================================================================
// SessionPersistence 测试
// =========================================================================

void test_session_save_load() {
    TEST("SessionPersistence — 保存和加载往返");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_session_enhanced";
    ProjectIO::createProjectDir(tmp, "测试");
    FileStorageBackend storage(tmp);

    agent::SessionPersistence sp(storage);
    llm::Memory conv;
    conv.addUser("消息一");
    conv.addAssistant("回复一");

    sp.save(conv);
    auto loaded = sp.load();

    CHECK(loaded.size() == 2);

    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// ContextAssembler 测试
// =========================================================================

void test_assemble_no_project() {
    TEST("ContextAssembler — 无 Project 时 system_prompt 为空");
    llm::Memory conv;
    conv.addUser("测试");
    agent::ContextAssembler assembler;
    agent::TokenBudget budget;
    auto result = assembler.assemble(nullptr, conv, budget);
    CHECK(result.system_prompt.empty());
    PASS();
}

void test_build_system_prompt() {
    TEST("ContextAssembler::buildSystemPrompt — 生成有效提示词");
    Project project;
    project.title = "测试小说";
    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    ch.order = 1;
    project.outline.chapters.push_back(ch);

    auto prompt = agent::ContextAssembler::buildSystemPrompt(project);
    CHECK(!prompt.empty());
    CHECK(prompt.find("测试小说") != std::string::npos);
    PASS();
}

void test_build_system_prompt_no_chapter() {
    TEST("ContextAssembler::buildSystemPrompt — 无章节返回项目概述");
    Project project;
    project.title = "极简项目";
    auto prompt = agent::ContextAssembler::buildSystemPrompt(project);
    CHECK(prompt.find("极简项目") != std::string::npos);
    PASS();
}

void test_total_tokens() {
    TEST("ContextAssembler — total_tokens 统计正确");
    llm::Memory conv;
    conv.addUser("测试消息");

    agent::ContextAssembler assembler;
    agent::TokenBudget budget;
    auto result = assembler.assemble(nullptr, conv, budget);
    CHECK(result.total_tokens > 0);
    PASS();
}

// =========================================================================
// TokenBudget 测试
// =========================================================================

void test_budget_evaluate() {
    TEST("TokenBudget — evaluate 阈值判断");
    agent::TokenBudget budget;
    budget.model_limit = 10000;

    CHECK(budget.evaluate(3000) == agent::ContextStatus::Normal);
    CHECK(budget.evaluate(6500) == agent::ContextStatus::Warning);
    CHECK(budget.evaluate(9000) == agent::ContextStatus::Critical);
    CHECK(budget.evaluate(9600) == agent::ContextStatus::AutoCompact);
    CHECK(budget.evaluate(10001) == agent::ContextStatus::Error);
    PASS();
}

void test_budget_needs_compaction() {
    TEST("TokenBudget — needsCompaction");
    agent::TokenBudget budget;
    budget.model_limit = 10000;

    CHECK(!budget.needsCompaction(5000));
    CHECK(budget.needsCompaction(9600));
    PASS();
}

void test_budget_usage_percent() {
    TEST("TokenBudget — usagePercent");
    agent::TokenBudget budget;
    budget.model_limit = 10000;

    CHECK(budget.usagePercent(6000) == 60);
    CHECK(budget.usagePercent(0) == 0);
    PASS();
}

// =========================================================================
// 消息保留（Pin）测试
// =========================================================================

void test_pin_unpin() {
    TEST("Memory — pin/unpin 往返");
    llm::Memory conv;
    conv.addUser("消息A");
    conv.addAssistant("消息B");
    conv.addUser("消息C");

    CHECK(conv.pinMessage(1));
    auto pinned = conv.pinnedIndices();
    CHECK(pinned.size() == 1);
    CHECK(pinned[0] == 1);

    CHECK(conv.unpinMessage(1));
    CHECK(conv.pinnedIndices().empty());
    PASS();
}

void test_pin_out_of_range() {
    TEST("Memory — pin 越界返回 false");
    llm::Memory conv;
    conv.addUser("只有一条");
    CHECK(!conv.pinMessage(99));
    CHECK(!conv.unpinMessage(99));
    PASS();
}

// =========================================================================
// Compactor 测试
// =========================================================================

void test_compact_returns_retained() {
    TEST("Compactor — 压缩后返回 retained 消息列表");
    agent::Compactor compactor;
    CompactMockLLMClient llm;

    std::vector<llm::Message> messages;
    for (int i = 0; i < 15; ++i) {
        messages.push_back(llm::Message::user("用户消息 " + std::to_string(i)));
        messages.push_back(llm::Message::assistant("助手回复 " + std::to_string(i)));
    }
    CHECK(messages.size() == 30);

    auto result = compactor.compact(messages, llm);

    CHECK(result.messages_compacted == 20);
    // retained = 2 (摘要对) + 10 (保留的最近 5 对)
    CHECK(result.retained.size() == 12);
    CHECK(result.retained[0].content.find("以下是被压缩的旧对话摘要") != std::string::npos);
    CHECK(result.retained[1].content.find("被压缩的历史摘要") != std::string::npos);
    CHECK(result.summary.find("压缩摘要") != std::string::npos);
    PASS();
}

void test_compact_insufficient_messages() {
    TEST("Compactor — 消息不足时不压缩");
    agent::Compactor compactor;
    CompactMockLLMClient llm;

    std::vector<llm::Message> messages = { llm::Message::user("仅一条") };
    auto result = compactor.compact(messages, llm);

    CHECK(result.messages_compacted == 0);
    CHECK(result.retained.size() == 1);
    PASS();
}

// =========================================================================
// ContextAssembler 降级可见性
// =========================================================================

void test_critical_warning() {
    TEST("ContextAssembler — 超限时生成致命错误");
    agent::ContextAssembler assembler;
    agent::TokenBudget budget;
    budget.model_limit = 200;

    llm::Memory conv;
    std::string big_text = "word ";
    for (int i = 0; i < 600; ++i) big_text += "word ";
    conv.addUser(big_text);

    auto result = assembler.assemble(nullptr, conv, budget);

    CHECK(result.fatal);
    bool has_error = false;
    for (const auto& w : result.warnings) {
        if (w.find("超过模型上限") != std::string::npos) has_error = true;
    }
    CHECK(has_error);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_context_manager (重构版) ===\n\n";

    // SessionPersistence
    test_session_save_load();

    // ContextAssembler
    test_assemble_no_project();
    test_build_system_prompt();
    test_build_system_prompt_no_chapter();
    test_total_tokens();

    // TokenBudget
    test_budget_evaluate();
    test_budget_needs_compaction();
    test_budget_usage_percent();

    // Pin
    test_pin_unpin();
    test_pin_out_of_range();

    // Compactor
    test_compact_returns_retained();
    test_compact_insufficient_messages();

    // 降级可见性
    test_critical_warning();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
