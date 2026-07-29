// CoreLoop 测试 — 核心功能测试。

#include "agent/core/CoreLoop.h"
#include "agent/tool/IToolProvider.h"
#include "agent/tool/ToolRegistry.h"
#include "agent/tool/ToolPipeline.h"
#include "agent/core/AgentState.h"
#include "agent/context/Compactor.h"
#include "agent/context/TokenBudget.h"
#include "llm/ILLMClient.h"
#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "llm/TokenCounter.h"
#include "config/AppConfig.h"

#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
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

// Mock 大结果工具 — 模拟单次返回大体积内容（轮内累积溢出的典型来源）
class MockBigTool : public agent::BuiltInTool {
public:
    std::string name() const override { return "read_big"; }
    std::string description() const override { return "读取大体积内容"; }
    json parameters() const override { return json::object(); }
    json execute(const json&) override {
        // 约 3500 个英文单词 ≈ 4550 token（TokenCounter 按单词×1.3 估算）
        std::string big;
        big.reserve(21000);
        for (int i = 0; i < 3500; ++i) big += "lorem ";
        return {{"content", big}};
    }
    agent::ToolCategory category() const override { return agent::ToolCategory::Content; }
};

// Compactor 摘要生成专用 mock — 与主循环的 MockSeqLLMClient 隔离，
// 避免轮内压缩消耗主循环的响应序列。
class SummaryMockLLMClient : public llm::ILLMClient {
public:
    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks,
                          const std::atomic<bool>*) override { return summary(); }
    llm::LLMResponse chatNonStreaming(const std::vector<llm::Message>&,
                                       const std::vector<llm::ToolDefinition>&,
                                       const std::string&) override { return summary(); }
    const ProviderConfig& config() const override { static ProviderConfig c; return c; }
private:
    static llm::LLMResponse summary() {
        llm::LLMResponse r;
        r.content = "【摘要】旧历史已压缩。";
        r.finish_reason = "stop";
        return r;
    }
};

// 校验消息序列符合 OpenAI 协议：tool 消息必须紧随携带匹配 tool_call_id 的
// assistant 消息，且每个 tool_call 在下一条非 tool 消息前都有结果。
static bool isLegalMessageSequence(const std::vector<llm::Message>& msgs) {
    std::set<std::string> pending;
    for (const auto& m : msgs) {
        if (m.role == llm::MessageRole::Tool) {
            if (pending.erase(m.tool_call_id) == 0) return false;  // 孤儿 tool 消息
        } else {
            if (!pending.empty()) return false;  // 上一组 tool_calls 未全部回应
            if (m.role == llm::MessageRole::Assistant)
                for (const auto& tc : m.tool_calls) pending.insert(tc.id);
        }
    }
    return pending.empty();
}

// ===========================================================================
// 测试
// ===========================================================================

void test_basic_tool_call() {
    TEST("CoreLoop — 基本工具调用");
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
    llm::Memory conv;
    conv.addUser("帮我读第1章");

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    agent::CoreLoopConfig cfg;
    cfg.max_rounds = 5;

    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, cfg);
    CHECK(!result.response.content.empty());
    CHECK(g_read_calls == 1);
    CHECK(result.rounds_executed == 2);  // round=0(tool_call) + round=1(文本回复) = 2 轮
    PASS();
}

void test_direct_text_response() {
    TEST("CoreLoop — 直接文本回复无工具调用");
    MockSeqLLMClient client;
    {
        llm::LLMResponse r;
        r.content = "好的，我明白了。";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    llm::Memory conv;
    conv.addUser("你好");

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, {});
    CHECK(result.response.content == "好的，我明白了。");
    CHECK(result.rounds_executed == 1);  // round=0 首轮就结束 = 1 轮
    PASS();
}

void test_cancellation() {
    TEST("CoreLoop — 外部取消信号");
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
    llm::Memory conv;
    conv.addUser("测试取消");

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    loop.setCancelled(&cancel_flag);
    cancel_flag.store(true);  // 立即取消

    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, {});
    CHECK(result.cancelled == true);
    PASS();
}

void test_state_machine_transitions() {
    TEST("CoreLoop — 状态机转换");
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
    llm::Memory conv;
    conv.addUser("测试");

    g_read_calls = 0;
    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline, &state_machine);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, {});
    (void)result;

    // CoreLoop 管理 Thinking↔AwaitingTool 转换，最终停留在 Thinking。
    // 由 Agent/SerialProcessor 负责调用 transition(Idle)。
    CHECK(state_machine.current() == agent::AgentState::Thinking);
    PASS();
}

void test_reasoning_content_preserved() {
    TEST("CoreLoop — reasoning_content 在工具调用循环中保留");
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
    llm::Memory conv;
    conv.addUser("帮我分析第1章");

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    agent::CoreLoopConfig cfg;
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

void test_tool_lifecycle_callbacks() {
    TEST("CoreLoop — on_tool_start/on_tool_finish 回调");
    MockSeqLLMClient client;
    // 第 1 轮：返回 1 个 tool_call
    {
        llm::LLMResponse r;
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    // 第 2 轮：返回文本
    {
        llm::LLMResponse r;
        r.content = "完成";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    auto mockTool = std::make_unique<MockReadTool>();
    registry.registerBuiltInTool(std::move(mockTool));

    llm::Memory conv;
    conv.addUser("读一下第一章");

    std::vector<std::string> started;
    std::vector<std::string> finished;
    bool all_ok = true;
    llm::StreamCallbacks cb;
    cb.on_tool_start = [&](const std::string& name) { started.push_back(name); };
    cb.on_tool_finish = [&](const std::string& name, bool ok) {
        finished.push_back(name);
        all_ok = all_ok && ok;
    };

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", cb, {});
    CHECK(result.rounds_executed == 2);
    CHECK(started.size() == 1);
    CHECK(started[0] == "read_chapter");
    CHECK(finished == started);
    CHECK(all_ok);
    PASS();
}

// ===========================================================================
// 轮内累积溢出预防（on_tool_results_applied hook）
// ===========================================================================

void test_inround_compaction_on_big_tool_results() {
    TEST("CoreLoop — 大工具结果触发轮内压缩且后续序列合法");
    MockSeqLLMClient client;
    // 第 1 轮：调用大结果工具
    {
        llm::LLMResponse r;
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_big";
        tc.arguments = "{}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    // 第 2 轮：最终文本（压缩后序列合法才能走到这里）
    {
        llm::LLMResponse r;
        r.content = "完成";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(std::make_unique<MockBigTool>());

    llm::Memory conv;
    // 预填历史对话，为 Compactor 提供可压缩区
    for (int i = 0; i < 6; ++i) {
        conv.addUser("历史问题内容较长用于制造可压缩区域第" + std::to_string(i) + "条");
        conv.addAssistant("历史回答内容较长用于制造可压缩区域第" + std::to_string(i) + "条");
    }
    conv.addUser("读取大文件");

    // 大结果回填后约 4800 token：超过 5000×80%=4000 的 AutoCompact 阈值，
    // 但压缩后（保留窗口含大结果 ≈ 4600）仍低于模型上限 → 循环应继续
    agent::TokenBudget budget;
    budget.model_limit = 5000;
    agent::Compactor compactor({.keep_exchanges = 2, .min_keep = 1});
    SummaryMockLLMClient summary_llm;

    int hook_calls = 0;
    bool compacted = false;
    agent::CoreLoopConfig cfg;
    cfg.max_rounds = 5;
    // 与 Agent::processSerial 中的真实 hook 同构：评估 → 压缩 → 复评
    cfg.hooks.on_tool_results_applied = [&]() -> bool {
        ++hook_calls;
        int total = llm::TokenCounter::countMessages(conv.messages());
        if (!budget.needsCompaction(total)) return true;
        auto cr = compactor.compact(conv.messages(), summary_llm);
        if (cr.messages_compacted > 0) {
            compacted = true;
            conv.clear();
            for (auto& m : cr.retained) conv.inject(std::move(m));
        }
        int after = llm::TokenCounter::countMessages(conv.messages());
        return budget.evaluate(after) < agent::ContextStatus::Error;
    };

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, cfg);

    CHECK(hook_calls == 1);
    CHECK(compacted);                          // 轮内压缩确实发生
    CHECK(!result.budget_exhausted);           // 压缩后回到安全水位，循环继续
    CHECK(result.response.content == "完成");
    CHECK(result.rounds_executed == 2);
    CHECK(isLegalMessageSequence(conv.messages()));  // 压缩后无孤儿 tool 消息
    // 摘要对已替换旧历史
    bool has_summary = false;
    for (const auto& m : conv.messages())
        if (m.content.find("被压缩的历史摘要") != std::string::npos) has_summary = true;
    CHECK(has_summary);
    PASS();
}

void test_budget_exhausted_graceful_stop() {
    TEST("CoreLoop — 压缩不足时优雅终止而非抛异常");
    MockSeqLLMClient client;
    {
        llm::LLMResponse r;
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_big";
        tc.arguments = "{}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    // 若循环未终止会消费这条响应 —— 用 callCount 验证未发起下一轮
    {
        llm::LLMResponse r;
        r.content = "不应到达";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(std::make_unique<MockBigTool>());

    llm::Memory conv;
    conv.addUser("读取大文件");

    agent::CoreLoopConfig cfg;
    cfg.max_rounds = 5;
    // 模拟 Agent 侧“压缩后仍超模型上限”：hook 返回 false 要求终止
    cfg.hooks.on_tool_results_applied = [&]() -> bool { return false; };

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, cfg);

    CHECK(result.budget_exhausted);
    CHECK(result.response.tool_calls.empty());   // 已剥离，上层不会追加孤儿 assistant
    CHECK(result.rounds_executed == 1);
    CHECK(!result.error.empty());
    CHECK(client.callCount() == 1);              // 未发起注定超限的下一轮 LLM 调用
    CHECK(isLegalMessageSequence(conv.messages()));  // tool_result 已入 memory 且配对完整
    CHECK(conv.messages().back().role == llm::MessageRole::Tool);
    PASS();
}

void test_small_results_no_compaction() {
    TEST("CoreLoop — 小结果路径不触发压缩（回归保护）");
    MockSeqLLMClient client;
    {
        llm::LLMResponse r;
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    {
        llm::LLMResponse r;
        r.content = "章节总结";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    registry.registerBuiltInTool(std::make_unique<MockReadTool>());

    g_read_calls = 0;
    llm::Memory conv;
    conv.addUser("帮我读第1章");

    agent::TokenBudget budget;  // 默认 131072 上限，小结果远不及阈值
    int hook_calls = 0;
    bool compacted = false;
    agent::CoreLoopConfig cfg;
    cfg.max_rounds = 5;
    cfg.hooks.on_tool_results_applied = [&]() -> bool {
        ++hook_calls;
        if (budget.needsCompaction(llm::TokenCounter::countMessages(conv.messages()))) {
            compacted = true;  // 不应到达
        }
        return true;
    };

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", {}, cfg);

    // 行为与 test_basic_tool_call 完全一致：hook 存在但不改变任何路径
    CHECK(hook_calls == 1);
    CHECK(!compacted);
    CHECK(!result.budget_exhausted);
    CHECK(result.response.content == "章节总结");
    CHECK(result.rounds_executed == 2);
    CHECK(g_read_calls == 1);
    CHECK(conv.messages().size() == 3);  // user + assistant(tc) + tool，未被压缩改写
    CHECK(isLegalMessageSequence(conv.messages()));
    PASS();
}

int main() {
    std::cout << "CoreLoop 测试:\n";

    test_basic_tool_call();
    test_direct_text_response();
    test_reasoning_content_preserved();
    test_cancellation();
    test_state_machine_transitions();
    test_tool_lifecycle_callbacks();
    test_inround_compaction_on_big_tool_results();
    test_budget_exhausted_graceful_stop();
    test_small_results_no_compaction();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 通过\n";
    return (tests_run == tests_passed) ? 0 : 1;
}
