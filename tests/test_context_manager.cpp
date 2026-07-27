// test_context_manager — 测试新拆分的上下文组件（Compactor + ContextAssembler + TokenBudget + SessionPersistence）。

#include "agent/context/Compactor.h"
#include "agent/context/ContextBudgetEvaluator.h"
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

void test_session_fidelity() {
    TEST("SessionPersistence — system 不落盘 + reasoning/preserved 往返");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_session_fidelity";
    ProjectIO::createProjectDir(tmp, "测试");
    FileStorageBackend storage(tmp);

    agent::SessionPersistence sp(storage);
    llm::Memory conv;
    conv.setSystemPrompt("旧的 system prompt");
    conv.addUser("问题");
    llm::Message assistant = llm::Message::assistant("回答");
    assistant.reasoning_content = "推理过程";
    conv.inject(std::move(assistant));
    conv.pin(1);  // pin user 消息（含 system 偏移，对应 messages()[0]）

    sp.save(conv);
    auto loaded = sp.load();

    // system prompt 不应被持久化；两条对话消息完整恢复
    CHECK(loaded.systemPrompt().empty());
    CHECK(loaded.messages().size() == 2);
    CHECK(loaded.messages()[0].preserved);
    CHECK(loaded.messages()[1].reasoning_content == "推理过程");

    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// ContextBudgetEvaluator 测试
// =========================================================================

void test_evaluate_total_tokens() {
    TEST("ContextBudgetEvaluator — total_tokens 统计正确");
    llm::Memory conv;
    conv.addUser("测试消息");

    agent::ContextBudgetEvaluator evaluator;
    agent::TokenBudget budget;
    auto result = evaluator.evaluate(conv, budget, conv.systemPrompt());
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
    CHECK(budget.evaluate(7500) == agent::ContextStatus::Critical);
    CHECK(budget.evaluate(9000) == agent::ContextStatus::AutoCompact);
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

    CHECK(conv.pin(1));
    auto pinned = conv.pinnedIndices();
    CHECK(pinned.size() == 1);
    CHECK(pinned[0] == 1);

    CHECK(conv.unpin(1));
    CHECK(conv.pinnedIndices().empty());
    PASS();
}

void test_pin_out_of_range() {
    TEST("Memory — pin 越界返回 false");
    llm::Memory conv;
    conv.addUser("只有一条");
    CHECK(!conv.pin(99));
    CHECK(!conv.unpin(99));
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
// ContextBudgetEvaluator 降级可见性
// =========================================================================

void test_critical_warning() {
    TEST("ContextBudgetEvaluator — 超限时生成致命错误");
    agent::ContextBudgetEvaluator evaluator;
    agent::TokenBudget budget;
    budget.model_limit = 200;

    llm::Memory conv;
    std::string big_text = "word ";
    for (int i = 0; i < 600; ++i) big_text += "word ";
    conv.addUser(big_text);

    auto result = evaluator.evaluate(conv, budget, conv.systemPrompt());

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
    test_session_fidelity();

    // ContextBudgetEvaluator
    test_evaluate_total_tokens();

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
