#include "agent/core/Agent.h"
#include "agent/tool/ToolRegistry.h"
#include "agent/session/SessionPersistence.h"
#include "agent/context/Memory.h"
#include "llm/LLMClientFactory.h"
#include "project/FileStorageBackend.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"
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
    cfg.max_context_tokens = 65536;
    return cfg;
}

// 解析请求体中的 "stream" 字段，判断是流式还是非流式请求。
static bool isStreamRequest(const httplib::Request& req) {
    try {
        auto j = json::parse(req.body);
        return j.value("stream", false);
    } catch (...) {
        return false;
    }
}

// 构造非流式 JSON 响应（纯文本）。
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

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    auto response = agent.process("Hi");

    CHECK(response.content == "你好，我是助手");
    CHECK(response.finish_reason == "stop");

    // 验证对话历史：user + assistant = 2 条
    CHECK(agent.memory().size() == 2);
    CHECK(agent.memory().all()[0].role == llm::MessageRole::User);
    CHECK(agent.memory().all()[0].content == "Hi");
    CHECK(agent.memory().all()[1].role == llm::MessageRole::Assistant);
    CHECK(agent.memory().all()[1].content == "你好，我是助手");

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
            // 第二次：流式，返回最终文本
            std::string body = llm::test::sseContentChunk("工具已执行完毕") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
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

    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);
    auto response = agent.process("echo test");

    CHECK(response.content == "工具已执行完毕");
    CHECK(call_count == 2);

    // 验证对话历史：user + assistant(tool_calls) + tool_result + assistant(final)
    CHECK(agent.memory().size() == 4);
    CHECK(agent.memory().all()[0].role == llm::MessageRole::User);
    CHECK(agent.memory().all()[1].role == llm::MessageRole::Assistant);
    CHECK(agent.memory().all()[1].tool_calls.size() == 1);
    CHECK(agent.memory().all()[2].role == llm::MessageRole::Tool);
    CHECK(agent.memory().all()[3].role == llm::MessageRole::Assistant);
    CHECK(agent.memory().all()[3].content == "工具已执行完毕");

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

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    auto response = agent.execute("查询项目状态");

    CHECK(response.content == "查询结果");
    // execute 不修改内部对话历史
    CHECK(agent.memory().empty());

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

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    // 第一轮
    agent.process("第一句话");
    CHECK(agent.memory().size() == 2);

    // 第二轮
    agent.process("第二句话");
    CHECK(agent.memory().size() == 4);

    // 清空
    agent.clearMemory();
    CHECK(agent.memory().empty());

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

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    auto response = agent.process("");
    CHECK(response.content.empty());
    CHECK(agent.memory().empty());

    server.stop();
    PASS();
}

// =========================================================================
// 测试: B8 — 异常后状态恢复（不卡 Thinking 永久拒输入）
// =========================================================================

void test_exception_recovery() {
    TEST("B8 — 异常后 agent.canAcceptInput() 恢复为 true");

    MockServer server;
    // 模拟 LLM 服务端异常：返回非法 JSON，导致 SSEParser 抛 json::parse_error
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            res.set_content("not valid sse", "text/event-stream");
        } else {
            res.set_content("{ broken json!!! ", "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    // 异常前状态正确
    CHECK(agent.canAcceptInput());

    // 调用 processUserMessage — 内部应捕获异常并恢复
    auto response = agent.process("应该不崩溃");
    // 异常恢复后返回空响应
    CHECK(response.content.empty());

    // B8 关键断言：异常后必须能接受新输入（状态已恢复为 Idle）
    CHECK(agent.canAcceptInput());
    CHECK(agent.currentState() == agent::AgentState::Idle);

    server.stop();
    PASS();
}

// =========================================================================

void test_session_persisted_after_message() {
    TEST("B2 — processUserMessage 后会话增量落盘到 conversation.json");

    // 准备一个临时项目目录
    const std::string tmp = "D:/C++Code/C++NovelAgent/build/tmp_test_b2_persist";
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "B2 测试");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("第二章开头") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("第二章开头"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    // 绑定 SessionPersistence，使 processUserMessage 末尾的 saveSessionState 真正落盘
    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);
    agent.setPersistence(&persistence);

    // 处理前：conversation.json 应为空数组（createProjectDir 初始化的默认值）
    const std::string convPath = tmp + "/.novelagent/conversation.json";
    json before = json::parse(utils::file::readText(convPath));
    CHECK(before.is_array() && before.empty());

    auto response = agent.process("帮我写第二章开头");
    CHECK(response.content == "第二章开头");

    // 处理后：conversation.json 应已被增量保存，含本轮 user + assistant 两条消息
    json after = json::parse(utils::file::readText(convPath));
    CHECK(after.is_array());
    CHECK(after.size() == 2);
    CHECK(after[0]["role"] == "user");
    CHECK(after[0]["content"] == "帮我写第二章开头");
    CHECK(after[1]["role"] == "assistant");

    server.stop();
    utils::file::removeDir(tmp);
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
    test_session_persisted_after_message();
    test_exception_recovery();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
