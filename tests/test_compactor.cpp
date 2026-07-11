// Compactor 测试 — MED-1 修复验证 + 基本功能测试。

#include "agent/Compactor.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "config/AppConfig.h"

#include <cassert>
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

using json = nlohmann::json;

// Mock LLMClient — 返回固定摘要。
class MockCompactorClient : public llm::ILLMClient {
public:
    explicit MockCompactorClient(std::string summary = "主角决定离开村庄，踏上冒险旅程。导师给了他一个神秘任务。")
        : summary_(std::move(summary)) {}

    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks) override {
        return makeResponse();
    }
    llm::LLMResponse chatNonStreaming(const std::vector<llm::Message>&,
                                       const std::vector<llm::ToolDefinition>&,
                                       const std::string&) override {
        return makeResponse();
    }
    const ProviderConfig& config() const override { static ProviderConfig c; return c; }
private:
    std::string summary_;
    llm::LLMResponse makeResponse() const {
        llm::LLMResponse r;
        r.content = summary_;
        r.finish_reason = "stop";
        return r;
    }
};

void test_compact_basic() {
    TEST("Compactor — 基本压缩");
    agent::Compactor compactor;
    MockCompactorClient client;
    llm::Conversation conv;

    // 添加 30 条消息（超过保留 20 条的阈值）
    for (int i = 0; i < 30; ++i) {
        conv.addUser("用户消息 #" + std::to_string(i));
        conv.addAssistant("助手回复 #" + std::to_string(i));
    }
    CHECK(conv.size() == 60);

    auto result = compactor.compact(conv, client);
    CHECK(result.messages_compacted > 0);
    CHECK(!result.summary.empty());
    CHECK(conv.size() < 60);  // 头部消息被删除
    PASS();
}

void test_compact_skip_too_few() {
    TEST("Compactor — 消息不足跳过 (≤1 条)");
    agent::Compactor compactor;
    MockCompactorClient client;
    llm::Conversation conv;

    conv.addUser("你好");
    CHECK(conv.size() == 1);

    auto result = compactor.compact(conv, client);
    CHECK(result.messages_compacted == 0);
    CHECK(conv.size() == 1);  // 未压缩
    PASS();
}

void test_compact_few_messages() {
    TEST("Compactor — 消息少但仍压缩 (2 条 → 保留 1 压缩 1)");
    agent::Compactor compactor;
    MockCompactorClient client("摘要");
    llm::Conversation conv;

    conv.addUser("长文本消息1: " + std::string(2000, 'x'));
    conv.addAssistant("长文本消息2: " + std::string(2000, 'y'));
    CHECK(conv.size() == 2);

    auto result = compactor.compact(conv, client);
    // 总消息 2 条，保留 1 条，压缩 1 条
    CHECK(result.messages_compacted == 1);
    CHECK(!result.summary.empty());
    CHECK(conv.size() == 3);  // 保留 1 条 + 2 条摘要 user/assistant 对
    PASS();
}

void test_compact_summary_saved() {
    TEST("Compactor — 摘要保存和读取");
    agent::Compactor compactor;
    MockCompactorClient client;
    llm::Conversation conv;

    for (int i = 0; i < 25; ++i) {
        conv.addUser("msg" + std::to_string(i));
        conv.addAssistant("reply" + std::to_string(i));
    }

    compactor.compact(conv, client);
    CHECK(compactor.hasSummary());
    CHECK(!compactor.summary().empty());
    CHECK(compactor.marker() > 0);
    PASS();
}

void test_compact_clear() {
    TEST("Compactor — 清除摘要");
    agent::Compactor compactor;
    MockCompactorClient client;
    llm::Conversation conv;

    for (int i = 0; i < 25; ++i) {
        conv.addUser("msg" + std::to_string(i));
        conv.addAssistant("reply" + std::to_string(i));
    }

    compactor.compact(conv, client);
    CHECK(compactor.hasSummary());

    compactor.clear();
    CHECK(!compactor.hasSummary());
    CHECK(compactor.marker() == 0);
    PASS();
}

void test_compact_restore() {
    TEST("Compactor — 从持久化恢复");
    agent::Compactor compactor;

    compactor.restore("测试摘要", 15);
    CHECK(compactor.hasSummary());
    CHECK(compactor.summary() == "测试摘要");
    CHECK(compactor.marker() == 15);
    PASS();
}

void test_auto_compact_settings() {
    TEST("Compactor — 自动压缩配置");
    agent::Compactor compactor;

    compactor.setAutoCompact(true, 80);
    CHECK(compactor.shouldAutoCompact(85));   // 85 >= 80
    CHECK(!compactor.shouldAutoCompact(75));  // 75 < 80

    compactor.setAutoCompact(false);
    CHECK(!compactor.shouldAutoCompact(95));  // 关闭
    PASS();
}

void test_compact_with_focus() {
    TEST("Compactor — 带焦点的压缩");
    agent::Compactor compactor;
    MockCompactorClient client("【摘要】主角的内心矛盾");
    llm::Conversation conv;

    for (int i = 0; i < 25; ++i) {
        conv.addUser("第" + std::to_string(i) + "轮");
        conv.addAssistant("回答" + std::to_string(i));
    }

    auto result = compactor.compact(conv, client,
                                     std::optional<std::string>("关注角色动机"));
    CHECK(result.messages_compacted > 0);
    PASS();
}

int main() {
    std::cout << "Compactor 测试:\n";

    test_compact_basic();
    test_compact_skip_too_few();
    test_compact_few_messages();
    test_compact_summary_saved();
    test_compact_clear();
    test_compact_restore();
    test_auto_compact_settings();
    test_compact_with_focus();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 通过\n";
    return (tests_run == tests_passed) ? 0 : 1;
}
