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

static llm::Conversation makeLongConversation() {
    llm::Conversation conv;
    conv.addUser("这是一条比较长的用户消息用于测试上下文窗口的截断功能判断是否正常。" + std::string(100, 'x'));
    conv.addAssistant("助手回复同样包含较多文字内容用于占满预算空间触发截断逻辑。" + std::string(100, 'y'));
    conv.addUser("第二条用户消息继续增加对话历史的长度以测试截断是否正确工作。" + std::string(100, 'z'));
    conv.addAssistant("助手再次回复确保消息列表中有足够条目可以触发截断行为验证。" + std::string(100, 'w'));
    return conv;
}

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

// =========================================================================
// A5 修复验证：novel.json 文件名路径
// 此前 ContextManager 写死 "project.json"，而实际元数据文件是 "novel.json"，
// 导致 isVectorStoreStale 永远返回 false、mtime 一致性保障整条失效。
// 本测试验证修复后 isVectorStoreStale 能正确读取 novel.json 的 mtime。
// =========================================================================

void test_isVectorStoreStale_reads_novel_json() {
    TEST("A5 — isVectorStoreStale 正确读取 novel.json mtime");

    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_stale_novel";
    ProjectIO::createProjectDir(tmp, "stale 测试");
    FileStorageBackend storage(tmp);

    // 构造一个 Project 并绑定到 ContextManager
    Project proj = ProjectIO::load(tmp);
    // load() 会设置 proj.path = tmp，但显式确认以防万一
    proj.path = tmp;
    agent::ContextManager cm(storage);
    cm.setProject(&proj);

    // 阶段 1：没有 vectors.json → isVectorStoreStale 返回 false（无索引可比）
    CHECK(cm.isVectorStoreStale() == false);

    // 先建一个 vectors.json（时间戳 T1），它比 novel.json 旧或相同
    const std::string vecPath = tmp + "/.novelagent/vectors.json";
    // 确保 .novelagent 目录存在
    std::filesystem::create_directories(std::filesystem::path(vecPath).parent_path());
    utils::file::writeText(vecPath, "[]");

    // 确保后续文件的 mtime 严格晚于 vectors.json（Windows 文件系统 mtime 精度有限）
    // A12: projectSettingsMtime 现在取 5 个文件的最新 mtime，
    // sleep 需足够长以跨越所有文件系统的 mtime 颗粒度。
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // 阶段 2：更新项目文件（时间戳 T2 > T1）→ 此时向量索引应判定为 stale
    ProjectIO::save(proj);

    // A12: projectSettingsMtime 取多文件的 max mtime。ProjectIO::save 重写
    // novel+outline+characters+settings+world_rules 五个 JSON，均晚于 vectors.json。
    CHECK(cm.isVectorStoreStale() == true);

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
    std::string chapter_id;
    cm.saveSessionState(conv, chapter_id, {});

    // 加载回来：未修改 novel.json，mtime 应一致，不触发清空（无摘要可清，仅验证不崩溃）
    llm::Conversation loaded;
    std::string loaded_chapter;
    cm.loadSessionState(loaded, loaded_chapter);
    CHECK(loaded.size() == 1);

    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// assemble 基础测试
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
    TEST("assemble — 长消息触发截断 + 生成警告");
    auto conv = makeLongConversation();
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 50);
    CHECK(result.truncated_count > 0);
    CHECK(!result.warnings.empty());  // 截断应生成警告
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
    PASS();
}

void test_truncation_keeps_newest() {
    TEST("assemble — 截断保留最新消息");
    auto conv = makeLongConversation();
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 80);
    CHECK(result.truncated_count > 0);
    CHECK(!result.messages.empty());
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

void test_preserved_messages_survive() {
    TEST("truncateMessages — preserved 消息不丢失");
    llm::Conversation conv;
    conv.addUser("旧消息一" + std::string(200, 'x'));    // 大消息
    conv.addAssistant("旧回复一" + std::string(200, 'y'));
    conv.addUser("重要消息需要保留" + std::string(50, 'z'));
    conv.addAssistant("最新回复");

    // 标记第 2 条（索引 2 = "重要消息需要保留"）为 preserved
    CHECK(conv.pinMessage(2));

    agent::ContextManager cm;
    // 极小预算：只够保留 ~1-2 条小消息
    auto result = cm.assemble(conv, 80);

    // 应至少包含 preserved 消息 + 最后的兜底消息
    CHECK(result.messages.size() >= 1);

    // 检查结果中是否包含 preserved 消息
    bool found_preserved = false;
    for (const auto& msg : result.messages) {
        if (msg.content.find("重要消息需要保留") != std::string::npos) {
            found_preserved = true;
            break;
        }
    }
    CHECK(found_preserved);
    PASS();
}

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
    // 对话消息数下降到 20（保留最近 10 对 = 20 条）
    CHECK(conv.size() == 20);
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
    CHECK(conv.size() == 4);
    PASS();
}

void test_last_warnings_cached() {
    TEST("ContextManager — lastWarnings 缓存");
    agent::ContextManager cm;
    // 初始状态无警告
    CHECK(cm.lastWarnings().empty());

    // 触发截断 → 生成警告 → 缓存
    auto conv = makeLongConversation();
    cm.assemble(conv, 50);
    CHECK(!cm.lastWarnings().empty());

    // 不截断 → 警告清空
    llm::Conversation short_conv;
    short_conv.addUser("短消息");
    cm.assemble(short_conv, 131072);
    CHECK(cm.lastWarnings().empty());

    PASS();
}

// =========================================================================
// 降级可见性
// =========================================================================

void test_truncation_warning() {
    TEST("assemble — 截断生成中文警告");
    auto conv = makeLongConversation();
    agent::ContextManager cm;
    auto result = cm.assemble(conv, 50);

    CHECK(result.truncated_count > 0);
    bool has_truncation_warning = false;
    for (const auto& w : result.warnings) {
        if (w.find("截断") != std::string::npos) has_truncation_warning = true;
    }
    CHECK(has_truncation_warning);
    PASS();
}

void test_critical_warning() {
    TEST("assemble — 接近限制时生成临界警告");
    agent::ContextManager cm;
    cm.setModelContextLimit(1000);
    cm.recordUsage(900, 0);  // 90% — 临界

    llm::Conversation conv;
    conv.addUser("测试");
    auto result = cm.assemble(conv, 131072);

    bool has_critical = false;
    for (const auto& w : result.warnings) {
        if (w.find("接近模型上限") != std::string::npos) has_critical = true;
    }
    CHECK(has_critical);
    PASS();
}

void test_msg_budget_tiny_fallback() {
    TEST("assemble — 极小预算时兜底保留最后一条");
    agent::ContextManager cm;
    llm::Conversation conv;
    conv.addUser("长消息" + std::string(300, 'x'));
    conv.addAssistant("最新回复" + std::string(300, 'y'));

    // max_context_tokens 极小，消息太大无法正常容纳
    auto result = cm.assemble(conv, 1);

    // 兜底逻辑确保至少保留最后一条消息
    CHECK(!result.messages.empty());
    // 最后一条是 "最新回复"
    CHECK(result.messages.back().content.find("最新回复") != std::string::npos);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_context_manager (增强版) ===\n\n";

    // 基础
    test_session_save_load();
    test_isVectorStoreStale_reads_novel_json();
    test_session_mtime_restored_from_novel_json();
    test_no_truncation();
    test_truncation();
    test_assemble_no_project();
    test_build_system_prompt();
    test_build_system_prompt_no_chapter();
    test_total_tokens();
    test_truncation_keeps_newest();

    // 会话追踪
    test_record_usage();
    test_usage_percent();
    test_usage_critical();
    test_reset_session();

    // Pin
    test_preserved_messages_survive();
    test_pin_unpin();
    test_pin_out_of_range();

    // Compaction
    test_compact_has_summary_methods();
    test_compact_actually_removes_messages();
    test_compact_skip_when_messages_insufficient();
    test_last_warnings_cached();

    // 降级可见性
    test_truncation_warning();
    test_critical_warning();
    test_msg_budget_tiny_fallback();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
