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

void test_multi_session_lifecycle() {
    TEST("SessionPersistence — 多会话新建/切换/删除生命周期");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_multi_session";
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "测试");
    FileStorageBackend storage(tmp);

    agent::SessionPersistence sp(storage);
    llm::Memory conv;
    conv.addUser("第一个会话的消息");
    conv.addAssistant("回复");
    sp.save(conv);

    // 首次 save 后：索引已建，active 会话自动标题来自首条 user 消息
    const std::string first_id = sp.activeSessionId();
    CHECK(!first_id.empty());
    auto sessions = sp.listSessions();
    CHECK(sessions.size() == 1);
    CHECK(!sessions[0].title.empty());

    // 新建会话：成为 active，列表变为 2 个
    const std::string second_id = sp.createSession();
    CHECK(second_id != first_id);
    CHECK(sp.activeSessionId() == second_id);
    CHECK(sp.listSessions().size() == 2);
    CHECK(sp.load().size() == 0);

    // 切回第一个会话：消息完整往返
    CHECK(sp.switchSession(first_id));
    CHECK(sp.activeSessionId() == first_id);
    auto loaded = sp.load();
    CHECK(loaded.size() == 2);
    CHECK(loaded.messages()[0].content == "第一个会话的消息");

    // 删除 active 会话：非空内容归档，active 自动切到剩余会话
    CHECK(sp.deleteSession(first_id));
    CHECK(utils::file::exists(
        tmp + "/.novelagent/archive/" + first_id + ".json"));
    CHECK(!utils::file::exists(
        tmp + "/.novelagent/sessions/" + first_id + ".json"));
    CHECK(sp.activeSessionId() == second_id);
    CHECK(sp.listSessions().size() == 1);

    // 删除不存在的会话应返回 false
    CHECK(!sp.deleteSession("s-nonexistent"));

    utils::file::removeDir(tmp);
    PASS();
}

void test_delete_last_session() {
    TEST("SessionPersistence — 删除唯一会话自动新建 + 删除非 active 会话");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_del_last_session";
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "测试");
    FileStorageBackend storage(tmp);

    agent::SessionPersistence sp(storage);
    llm::Memory conv;
    conv.addUser("唯一会话的消息");
    sp.save(conv);
    const std::string only_id = sp.activeSessionId();

    // 删除唯一会话：自动新建空会话并设为 active
    CHECK(sp.deleteSession(only_id));
    auto sessions = sp.listSessions();
    CHECK(sessions.size() == 1);
    CHECK(sessions[0].id != only_id);
    CHECK(sp.activeSessionId() == sessions[0].id);
    CHECK(sp.load().size() == 0);

    // 删除非 active 会话：active 不变
    const std::string active_before = sp.activeSessionId();
    llm::Memory conv2;
    conv2.addUser("active 会话内容");
    sp.save(conv2);
    const std::string other_id = sp.createSession();  // 新会话成为 active
    CHECK(sp.switchSession(active_before));
    CHECK(sp.deleteSession(other_id));                // 删除非 active
    CHECK(sp.activeSessionId() == active_before);
    CHECK(sp.listSessions().size() == 1);

    utils::file::removeDir(tmp);
    PASS();
}

void test_corrupt_index_recovery() {
    TEST("SessionPersistence — 损坏 index.json 扫描目录重建（不丢会话）");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_corrupt_index";
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "测试");
    FileStorageBackend storage(tmp);

    // 先建两个正常会话
    std::string first_id, second_id;
    {
        agent::SessionPersistence sp(storage);
        llm::Memory conv;
        conv.addUser("第一个会话的消息");
        sp.save(conv);
        first_id = sp.activeSessionId();
        second_id = sp.createSession();
        llm::Memory conv2;
        conv2.addUser("第二个会话的消息");
        sp.save(conv2);
    }

    const std::string index_path = tmp + "/.novelagent/sessions/index.json";

    // 场景 1：active 类型错误（数字）——旧实现会抛 type_error 炸穿启动路径
    utils::file::writeText(index_path, R"({"active": 123, "sessions": []})");
    {
        agent::SessionPersistence sp(storage);
        auto sessions = sp.listSessions();      // 不得抛异常
        CHECK(sessions.size() == 2);            // 磁盘上的会话文件全部找回
        std::string active = sp.activeSessionId();
        CHECK(active == first_id || active == second_id);
        // 会话内容完好，标题从消息重新提取
        bool found_first = false;
        for (const auto& s : sessions) {
            if (s.id == first_id) { found_first = true; CHECK(!s.title.empty()); }
        }
        CHECK(found_first);
    }

    // 场景 2：active 悬空引用（不在 sessions 列表中）——旧实现 save 会静默跳过索引更新
    utils::file::writeText(index_path,
        R"({"active": "s-ghost", "sessions": [{"id": ")" + first_id +
        R"(", "title": "旧标题", "created_at": "2026-01-01T00:00:00Z", "updated_at": "2026-01-01T00:00:00Z"}]})");
    {
        agent::SessionPersistence sp(storage);
        std::string active = sp.activeSessionId();
        CHECK(active == first_id || active == second_id);
        auto sessions = sp.listSessions();
        CHECK(sessions.size() == 2);
        // 重建时从旧索引回收了 first_id 的元数据
        for (const auto& s : sessions) {
            if (s.id == first_id) CHECK(s.title == "旧标题");
        }
    }

    // 场景 3：非法 JSON —— 同样扫描重建，会话不丢，且 save 不再静默跳过索引更新
    utils::file::writeText(index_path, "{ 损坏的 json !!!");
    {
        agent::SessionPersistence sp(storage);
        llm::Memory conv;
        conv.addUser("重建后的新消息");
        sp.save(conv);                          // 不得抛异常，索引正常更新
        CHECK(sp.listSessions().size() == 2);
        CHECK(sp.load().size() == 1);
    }

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
    test_multi_session_lifecycle();
    test_delete_last_session();
    test_corrupt_index_recovery();

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
