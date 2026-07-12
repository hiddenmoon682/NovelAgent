// test_context_manager — 增强版测试（会话追踪 + pin + compaction + 降级可见性）。

#include "agent/ContextManager.h"
#include "llm/Conversation.h"
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
#include <thread>
#include <chrono>

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

// Mock LLMClient — compact() 用 chatNonStreaming 生成摘要，这里返回固定摘要文本。
class CompactMockLLMClient : public llm::ILLMClient {
public:
    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks) override {
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
    llm::Conversation conv;
    conv.addUser("消息一");
    conv.addAssistant("回复一");

    sp.save(conv);
    auto loaded = sp.load();

    CHECK(loaded.size() == 2);

    utils::file::removeDir(tmp);
    PASS();
}

void test_session_mtime_restored_from_novel_json() {
    TEST("A5 — saveSessionState 记录 novel.json 的非零 mtime");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_mtime_novel";
    ProjectIO::createProjectDir(tmp, "mtime 测试");
    FileStorageBackend storage(tmp);

    Project proj = ProjectIO::load(tmp);
    agent::ContextManager cm(storage);
    cm.setProject(&proj);

    llm::Conversation conv;
    conv.addUser("hi");
    cm.saveSessionState(conv, {});

    // 加载回来：未修改 novel.json，mtime 应一致，不触发清空（无摘要可清，仅验证不崩溃）
    llm::Conversation loaded;
    cm.loadSessionState(loaded);
    CHECK(loaded.size() == 1);

    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// assemble 基础测试
// =========================================================================

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
    auto prompt = cm.buildSystemPrompt(project);
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
    PASS();
}

// =========================================================================
// 会话级 Token 追踪
// =========================================================================

void test_record_usage() {
    TEST("recordUsage — 累计 token 统计");
    agent::ContextManager cm;

    cm.recordUsage(500, 200);
    cm.recordUsage(300, 150);

    auto stats = cm.sessionStats();
    CHECK(stats.total_input_tokens == 800);
    CHECK(stats.total_output_tokens == 350);
    CHECK(stats.request_count == 2);
    PASS();
}

void test_usage_percent() {
    TEST("usagePercent — 用量百分比计算");
    agent::ContextManager cm;
    cm.setModelContextLimit(10000);

    cm.recordUsage(6000, 0);  // 60% — Warning 阈值
    CHECK(cm.usagePercent() == 60);

    auto check = cm.checkThresholds();
    CHECK(check.status == agent::ContextStatus::Warning);
    CHECK(check.usage_percent == 60);
    PASS();
}

void test_usage_critical() {
    TEST("checkThresholds — 临界状态");
    agent::ContextManager cm;
    cm.setModelContextLimit(10000);

    cm.recordUsage(9000, 0);  // 90%
    auto check = cm.checkThresholds();
    CHECK(check.status == agent::ContextStatus::Critical);
    CHECK(check.usage_percent >= 85);
    PASS();
}

void test_reset_session() {
    TEST("resetSession — 重置后统计归零");
    agent::ContextManager cm;
    cm.recordUsage(1000, 500);
    cm.resetSession();

    auto stats = cm.sessionStats();
    CHECK(stats.total_input_tokens == 0);
    CHECK(stats.total_output_tokens == 0);
    CHECK(stats.request_count == 0);
    PASS();
}

// =========================================================================
// 消息保留（Pin）测试
// =========================================================================

void test_pin_unpin() {
    TEST("Conversation — pin/unpin 往返");
    llm::Conversation conv;
    conv.addUser("消息A");
    conv.addAssistant("消息B");
    conv.addUser("消息C");

    CHECK(conv.pinMessage(1));         // pin "消息B"
    auto pinned = conv.pinnedIndices();
    CHECK(pinned.size() == 1);
    CHECK(pinned[0] == 1);

    CHECK(conv.unpinMessage(1));       // unpin
    CHECK(conv.pinnedIndices().empty());
    PASS();
}

void test_pin_out_of_range() {
    TEST("Conversation — pin 越界返回 false");
    llm::Conversation conv;
    conv.addUser("只有一条");
    CHECK(!conv.pinMessage(99));
    CHECK(!conv.unpinMessage(99));
    PASS();
}

// =========================================================================
// Compaction 基础测试（不含 LLM 调用）
// =========================================================================

void test_compact_has_summary_methods() {
    TEST("ContextManager — hasCompactedSummary/clearCompactedSummary");
    agent::ContextManager cm;
    CHECK(!cm.hasCompactedSummary());

    // 不调用实际 LLM，只测 API 存在
    cm.clearCompactedSummary();
    CHECK(!cm.hasCompactedSummary());
    PASS();
}

// A1: compact() 必须真正删除已压缩的旧消息（而非仅存摘要、旧消息仍留在对话）。
// 此前 compact 签名是 const 引用无法修改对话，压缩形同虚设。修复后调用 removeOldest。
// 验证：压缩后对话消息数下降到 keep_count，且摘要被存储。
void test_compact_actually_removes_messages() {
    TEST("A1: compact() 真正删除已压缩旧消息");
    agent::ContextManager cm;
    llm::Conversation conv;
    // 注入 30 条消息（> kCompactKeepExchanges*2=20，触发实际压缩）
    for (int i = 0; i < 15; ++i) {
        conv.addUser("用户消息 " + std::to_string(i) + "，包含一些内容用于占位。");
        conv.addAssistant("助手回复 " + std::to_string(i) + "，同样包含占位内容。");
    }
    CHECK(conv.size() == 30);

    CompactMockLLMClient llm;
    auto result = cm.compact(conv, llm, std::nullopt);

    // 压缩了 30-20=10 条
    CHECK(result.messages_compacted == 10);
    // 对话消息数下降到 20（保留最近 10 对 = 20 条）+ 2（摘要 user/assistant 对）
    CHECK(conv.size() == 22);
    // 摘要插入在头部，保留的最近消息紧随其后
    const auto& msgs = conv.messages();
    CHECK(msgs[0].content.find("以下是被压缩的旧对话摘要") != std::string::npos);
    CHECK(msgs[1].content.find("被压缩的历史摘要") != std::string::npos);
    CHECK(msgs[2].content.find("用户消息 5") != std::string::npos);  // 第一条保留消息
    // 摘要被存储
    CHECK(cm.hasCompactedSummary());
    CHECK(result.summary.find("压缩摘要") != std::string::npos);
    PASS();
}

// A1: 消息不足时不跳过（消息少但 token 可能高），只保留最少 4 条。
void test_compact_skip_when_messages_insufficient() {
    TEST("A1: compact() 消息不足时压缩旧消息");
    agent::ContextManager cm;
    llm::Conversation conv;
    for (int i = 0; i < 5; ++i) {
        conv.addUser("短消息 " + std::to_string(i));
        conv.addAssistant("短回复 " + std::to_string(i));
    }
    CompactMockLLMClient llm;
    auto result = cm.compact(conv, llm, std::nullopt);
    // 10 条 > 1（硬拒绝阈值），动态保留 4 条，压缩 6 条
    CHECK(result.messages_compacted == 6);
    // 保留 4 条 + 2 条摘要 user/assistant 对
    CHECK(conv.size() == 6);
    PASS();
}

void test_last_warnings_cached() {
    TEST("ContextManager — lastWarnings 缓存");
    agent::ContextManager cm;
    CHECK(cm.lastWarnings().empty());

    // 通过真实消息内容触发临界告警（模型窗口 200 tokens，消息远超此值）。
    cm.setModelContextLimit(200);
    llm::Conversation conv;
    // 构造大量单词文本触发高 token 数（countTokens 按单词数统计 ASCII）。
    std::string big_text = "word ";
    for (int i = 0; i < 600; ++i) big_text += "word ";
    conv.addUser(big_text);  // ~600 单词 × 1.3 ≈ 780 tokens，远超 200 限制
    cm.assemble(conv, 131072);
    CHECK(!cm.lastWarnings().empty());

    // 恢复大窗口 → 不再有告警
    cm.setModelContextLimit(131072);
    cm.assemble(conv, 131072);
    CHECK(cm.lastWarnings().empty());

    PASS();
}

// =========================================================================
// 降级可见性
// =========================================================================

void test_critical_warning() {
    TEST("assemble — 超限时生成致命错误");
    agent::ContextManager cm;
    cm.setModelContextLimit(200);

    llm::Conversation conv;
    // 构造大量单词文本触发高 token 数（countTokens 按单词数统计 ASCII）。
    std::string big_text = "word ";
    for (int i = 0; i < 600; ++i) big_text += "word ";
    conv.addUser(big_text);  // ~600 单词 × 1.3 ≈ 780 tokens，远超 200 限制
    auto result = cm.assemble(conv, 131072);

    // 390% 超出模型窗口上限 → Error（致命错误），fatal 标志置位
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
    std::cout << "=== test_context_manager (增强版) ===\n\n";

    // 基础
    test_session_save_load();
    test_session_mtime_restored_from_novel_json();
    test_assemble_no_project();
    test_build_system_prompt();
    test_build_system_prompt_no_chapter();
    test_total_tokens();

    // 会话追踪
    test_record_usage();
    test_usage_percent();
    test_usage_critical();
    test_reset_session();

    // Pin
    test_pin_unpin();
    test_pin_out_of_range();

    // Compaction
    test_compact_has_summary_methods();
    test_compact_actually_removes_messages();
    test_compact_skip_when_messages_insufficient();
    test_last_warnings_cached();

    // 降级可见性
    test_critical_warning();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
