#include "agent/Agent.h"
#include "agent/ToolRegistry.h"
#include "llm/LLMClient.h"
#include "test_sse_helpers.h"
#include "utils/SchemaUtils.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cassert>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <string>

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

// ── Mock 服务器辅助 ──

struct MockServer {
    httplib::Server svr;
    std::thread thread;
    int port = 0;

    void start() {
        port = svr.bind_to_any_port("localhost");
        thread = std::thread([this]() { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }

    void stop() {
        svr.stop();
        if (thread.joinable()) thread.join();
    }
};

static ProviderConfig makeConfig(int port) {
    ProviderConfig cfg;
    cfg.api_key = "test-key";
    cfg.base_url = "http://localhost:" + std::to_string(port);
    cfg.model = "test-model";
    cfg.temperature = 0.7;
    cfg.max_tokens = 1024;
    cfg.context_window = 65536;
    return cfg;
}

/// 解析请求体中的 "stream" 字段，判断是流式还是非流式请求。
static bool isStreamRequest(const httplib::Request& req) {
    try {
        auto j = json::parse(req.body);
        return j.value("stream", false);
    } catch (...) {
        return false;
    }
}

/// 构造非流式 JSON 响应（纯文本）。
static std::string nonStreamJson(const std::string& content,
                                  const std::string& finish_reason = "stop") {
    json j;
    j["id"] = "chatcmpl-test";
    j["model"] = "test-model";
    j["choices"] = json::array({{
        {"index", 0},
        {"message", {{"role", "assistant"}, {"content", content}}},
        {"finish_reason", finish_reason}
    }});
    j["usage"] = {{"prompt_tokens", 10}, {"completion_tokens", 5}, {"total_tokens", 15}};
    return j.dump();
}

// =========================================================================
// 测试 1: 简单对话（无工具调用）
// =========================================================================

void test_simple_conversation() {
    TEST("简单对话 — 无工具调用");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            // 流式请求 → SSE
            std::string body = llm::test::sseContentChunk("你好") +
                               llm::test::sseContentChunk("，我是助手") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("你好，我是助手"), "application/json");
        }
    });
    server.start();

    llm::LLMClient client(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(client, registry);

    auto response = agent.processUserMessage("Hi");

    CHECK(response.content == "你好，我是助手");
    CHECK(response.finish_reason == "stop");

    // 验证对话历史：user + assistant = 2 条
    CHECK(agent.conversation().size() == 2);
    CHECK(agent.conversation().all()[0].role == llm::MessageRole::User);
    CHECK(agent.conversation().all()[0].content == "Hi");
    CHECK(agent.conversation().all()[1].role == llm::MessageRole::Assistant);
    CHECK(agent.conversation().all()[1].content == "你好，我是助手");

    server.stop();
    PASS();
}

// =========================================================================
// 测试 2: 单次 tool call 循环
// =========================================================================

void test_tool_call_loop() {
    TEST("Tool call 循环 — 调用工具后获取最终回复");

    MockServer server;

    // 状态跟踪：第一次调用返回 tool_calls，第二次返回文本
    int call_count = 0;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        call_count++;
        if (call_count == 1) {
            // 首次：流式，返回 tool_calls（SSE 格式）
            json tc;
            tc["id"] = "call_001";
            tc["type"] = "function";
            tc["function"] = {{"name", "echo"}, {"arguments", R"({"message":"你好世界"})"}};

            std::string body =
                llm::test::sseToolCallChunk(0, "call_001", "echo",
                                             R"({"message":"你好世界"})") +
                llm::test::sseFinishChunk("tool_calls") +
                llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            // 第二次：非流式，返回最终文本
            res.set_content(nonStreamJson("工具已执行完毕"), "application/json");
        }
    });
    server.start();

    llm::LLMClient client(makeConfig(server.port));
    agent::ToolRegistry registry;

    // 注册 echo 工具
    registry.registerTool(
        "echo", "回显消息",
        utils::schema::object({
            {"message", utils::schema::stringProp("要回显的消息")}
        }, {"message"}),
        agent::ToolCategory::System,
        [](const json& args) -> json {
            return {{"echo", args.value("message", "")}};
        }
    );

    agent::Agent agent(client, registry);
    auto response = agent.processUserMessage("echo test");

    CHECK(response.content == "工具已执行完毕");
    CHECK(call_count == 2);

    // 验证对话历史：user + assistant(tool_calls) + tool_result + assistant(final)
    CHECK(agent.conversation().size() == 4);
    CHECK(agent.conversation().all()[0].role == llm::MessageRole::User);
    CHECK(agent.conversation().all()[1].role == llm::MessageRole::Assistant);
    CHECK(agent.conversation().all()[1].tool_calls.size() == 1);
    CHECK(agent.conversation().all()[2].role == llm::MessageRole::Tool);
    CHECK(agent.conversation().all()[3].role == llm::MessageRole::Assistant);
    CHECK(agent.conversation().all()[3].content == "工具已执行完毕");

    server.stop();
    PASS();
}

// =========================================================================
// 测试 3: execute 模式（不修改 conversation）
// =========================================================================

void test_execute_mode() {
    TEST("execute 模式 — 不修改内部对话历史");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("查询结果") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("查询结果"), "application/json");
        }
    });
    server.start();

    llm::LLMClient client(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(client, registry);

    auto response = agent.execute("查询项目状态");

    CHECK(response.content == "查询结果");
    // execute 不修改内部对话历史
    CHECK(agent.conversation().empty());

    server.stop();
    PASS();
}

// =========================================================================
// 测试 4: 对话管理
// =========================================================================

void test_conversation_management() {
    TEST("对话管理 — 多轮对话 + 清空");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("收到") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("收到"), "application/json");
        }
    });
    server.start();

    llm::LLMClient client(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(client, registry);

    // 第一轮
    agent.processUserMessage("第一句话");
    CHECK(agent.conversation().size() == 2);

    // 第二轮
    agent.processUserMessage("第二句话");
    CHECK(agent.conversation().size() == 4);

    // 清空
    agent.clearConversation();
    CHECK(agent.conversation().empty());

    server.stop();
    PASS();
}

// =========================================================================
// 测试 5: 空输入
// =========================================================================

void test_empty_input() {
    TEST("空输入 — 返回空响应，不修改对话历史");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        res.set_content(nonStreamJson("不应被调用"), "application/json");
    });
    server.start();

    llm::LLMClient client(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(client, registry);

    auto response = agent.processUserMessage("");
    CHECK(response.content.empty());
    CHECK(agent.conversation().empty());

    server.stop();
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_agent ===\n\n";

    test_simple_conversation();
    test_tool_call_loop();
    test_execute_mode();
    test_conversation_management();
    test_empty_input();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
