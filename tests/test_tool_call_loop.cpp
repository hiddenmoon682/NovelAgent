// ToolCallLoop 测试 — 核心功能测试。

#include "agent/ToolCallLoop.h"
#include "agent/IToolProvider.h"
#include "agent/ToolRegistry.h"
#include "agent/AgentState.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"
#include "config/AppConfig.h"

#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <memory>
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

// ===========================================================================
// Mock LLMClient — 可控的 tool_call 响应序列
// ===========================================================================
class MockSeqLLMClient : public llm::ILLMClient {
    int call_count_ = 0;
    std::vector<llm::LLMResponse> responses_;
public:
    void addResponse(llm::LLMResponse r) { responses_.push_back(std::move(r)); }

    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks,
                          const std::atomic<bool>*) override {
        return nextResponse();
    }
    llm::LLMResponse chatNonStreaming(const std::vector<llm::Message>&,
                                       const std::vector<llm::ToolDefinition>&,
                                       const std::string&) override {
        return nextResponse();
    }
    const ProviderConfig& config() const override { static ProviderConfig c; return c; }

    int callCount() const { return call_count_; }
private:
    llm::LLMResponse nextResponse() {
        if (call_count_ < static_cast<int>(responses_.size())) {
            return responses_[call_count_++];
        }
        // 耗尽时返回空
        llm::LLMResponse r;
        r.finish_reason = "stop";
        return r;
    }
};

// ===========================================================================
// Mock 工具 — 记录调用并返回固定结果
// ===========================================================================
static int g_read_calls = 0;
class MockReadTool : public agent::BuiltInTool {
public:
    std::string name() const override { return "read_chapter"; }
    std::string description() const override { return "读取章节"; }
    json parameters() const override { return json::object(); }
    json execute(const json&) override {
        ++g_read_calls;
        return {{"content", "这是第一章的内容。"}};
    }
    agent::ToolCategory category() const override { return agent::ToolCategory::Content; }
};

// ===========================================================================
// 测试
// ===========================================================================

void test_basic_tool_call() {
    TEST("ToolCallLoop — 基本工具调用");
    MockSeqLLMClient client;
    // 第 1 轮：返回 1 个 tool_call
    {
        llm::LLMResponse r;
        r.content = "";
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{\"chapter_id\":\"ch-001\"}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    // 第 2 轮：返回文本
    {
        llm::LLMResponse r;
        r.content = "章节总结";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    auto mockTool = std::make_unique<MockReadTool>();
    registry.registerBuiltInTool(std::move(mockTool));

    g_read_calls = 0;
    llm::Conversation conv;
    conv.addUser("帮我读第1章");

    agent::ToolCallLoop loop(client, registry);
    agent::ToolCallLoopConfig cfg;
    cfg.max_rounds = 5;

    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, cfg);
    CHECK(!result.response.content.empty());
    CHECK(g_read_calls == 1);
    CHECK(result.rounds_executed == 2);  // round=0(tool_call) + round=1(文本回复) = 2 轮
    PASS();
}

void test_direct_text_response() {
    TEST("ToolCallLoop — 直接文本回复无工具调用");
    MockSeqLLMClient client;
    {
        llm::LLMResponse r;
        r.content = "好的，我明白了。";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    llm::Conversation conv;
    conv.addUser("你好");

    agent::ToolCallLoop loop(client, registry);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, {});
    CHECK(result.response.content == "好的，我明白了。");
    CHECK(result.rounds_executed == 1);  // round=0 首轮就结束 = 1 轮
    PASS();
}

void test_cancellation() {
    TEST("ToolCallLoop — 外部取消信号");
    MockSeqLLMClient client;
    {
        llm::LLMResponse r;
        r.content = "";
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{\"chapter_id\":\"ch-001\"}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    {
        llm::LLMResponse r;
        r.content = "最终回复";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    auto mockTool = std::make_unique<MockReadTool>();
    registry.registerBuiltInTool(std::move(mockTool));

    std::atomic<bool> cancel_flag{false};
    llm::Conversation conv;
    conv.addUser("测试取消");

    agent::ToolCallLoop loop(client, registry);
    loop.setCancelled(&cancel_flag);
    cancel_flag.store(true);  // 立即取消

    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, {});
    CHECK(result.cancelled == true);
    PASS();
}

void test_state_machine_transitions() {
    TEST("ToolCallLoop — 状态机转换");
    MockSeqLLMClient client;
    {
        llm::LLMResponse r;
        r.content = "";
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{\"chapter_id\":\"ch-001\"}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    {
        llm::LLMResponse r;
        r.content = "完成";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    auto mockTool = std::make_unique<MockReadTool>();
    registry.registerBuiltInTool(std::move(mockTool));

    agent::StateMachine state_machine;
    state_machine.transition(agent::AgentState::Thinking);  // 模拟 SerialProcessor 的真实流程
    llm::Conversation conv;
    conv.addUser("测试");

    g_read_calls = 0;
    agent::ToolCallLoop loop(client, registry, &state_machine);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, {});
    (void)result;

    // ToolCallLoop 管理 Thinking↔AwaitingTool 转换，最终停留在 Thinking。
    // 由 Agent/SerialProcessor 负责调用 transition(Idle)。
    CHECK(state_machine.current() == agent::AgentState::Thinking);
    PASS();
}

void test_reasoning_content_preserved() {
    TEST("ToolCallLoop — reasoning_content 在工具调用循环中保留");
    MockSeqLLMClient client;
    // 第 1 轮：返回 reasoning_content + tool_call
    {
        llm::LLMResponse r;
        r.content = "";
        r.reasoning_content = "我需要先读取第一章的内容...";
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{\"chapter_id\":\"ch-001\"}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    // 第 2 轮：最终文本回复（无 tool_calls）
    {
        llm::LLMResponse r;
        r.content = "这是最终回复";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    auto mockTool = std::make_unique<MockReadTool>();
    registry.registerBuiltInTool(std::move(mockTool));

    g_read_calls = 0;
    llm::Conversation conv;
    conv.addUser("帮我分析第1章");

    agent::ToolCallLoop loop(client, registry);
    agent::ToolCallLoopConfig cfg;
    cfg.max_rounds = 5;

    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, cfg);
    CHECK(!result.response.content.empty());

    // 验证对话中保留了 reasoning_content（在 assistant 消息中）
    bool found_reasoning = false;
    for (const auto& msg : conv.messages()) {
        if (msg.role == llm::MessageRole::Assistant && !msg.tool_calls.empty()) {
            if (msg.reasoning_content == "我需要先读取第一章的内容...") {
                found_reasoning = true;
                break;
            }
        }
    }
    CHECK(found_reasoning);

    // 验证最终退出消息（无 tool_calls）未被加入 conversation（符合 discard 规则）
    // conversation 中应当只有 1 条带 tool_calls 的 assistant 消息，0 条不带 tool_calls 的
    int assistant_with_tc = 0, assistant_without_tc = 0;
    for (const auto& msg : conv.messages()) {
        if (msg.role == llm::MessageRole::Assistant && !msg.tool_calls.empty())
            ++assistant_with_tc;
        if (msg.role == llm::MessageRole::Assistant && msg.tool_calls.empty())
            ++assistant_without_tc;
    }
    CHECK(assistant_with_tc == 1);
    CHECK(assistant_without_tc == 0);
    PASS();
}

int main() {
    std::cout << "ToolCallLoop 测试:\n";

    test_basic_tool_call();
    test_direct_text_response();
    test_reasoning_content_preserved();
    test_cancellation();
    test_state_machine_transitions();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 通过\n";
    return (tests_run == tests_passed) ? 0 : 1;
}
