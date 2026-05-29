#include "llm/LLMClient.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

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

using namespace llm;

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
    cfg.api_key = "test-key-123";
    cfg.base_url = "http://localhost:" + std::to_string(port);
    cfg.model = "test-model";
    cfg.temperature = 0.7;
    cfg.max_tokens = 1024;
    cfg.context_window = 65536;
    return cfg;
}

// ── SSE 响应构造 ──

static std::string sseContentChunk(const std::string& content) {
    return "data: {\"id\":\"test-id\",\"model\":\"test\",\"created\":1234,"
           "\"choices\":[{\"index\":0,\"delta\":{\"content\":\""
           + content + "\"}}]}\n\n";
}

static std::string sseFinishChunk(const std::string& reason) {
    return "data: {\"choices\":[{\"index\":0,\"delta\":{},"
           "\"finish_reason\":\"" + reason + "\"}],"
           "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5,"
           "\"total_tokens\":15}}\n\n";
}

static const char* sseDone = "data: [DONE]\n\n";

// =========================================================================
// 测试 1: 流式 token 回调
// =========================================================================

void test_streaming_token_callback() {
    TEST("流式 chat() on_content 回调");
    MockServer server;

    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        std::string body =
            sseContentChunk("Hello") +
            sseContentChunk(" World") +
            sseFinishChunk("stop") +
            sseDone;
        res.set_content(body, "text/event-stream");
    });
    server.start();

    LLMClient client(makeConfig(server.port));
    std::string collected;
    LLMResponse response;

    StreamCallbacks cb;
    cb.on_content = [&](const std::string& delta) { collected += delta; };
    cb.on_complete = [&](const LLMResponse& r) { response = r; };

    response = client.chat({{MessageRole::User, "Hi"}}, {}, "You are helpful.", cb);

    CHECK(collected == "Hello World");
    CHECK(response.content == "Hello World");
    CHECK(response.model == "test");
    CHECK(response.prompt_tokens == 10);
    CHECK(response.total_tokens == 15);

    server.stop();
    PASS();
}

// =========================================================================
// 测试 2: 流式 tool_call 回调
// =========================================================================

void test_streaming_tool_call_callback() {
    TEST("流式 chat() tool_call 回调 + 累积");
    MockServer server;

    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        // 使用 nlohmann::json 构造 SSE 数据以正确处理 JSON 转义
        using json = nlohmann::json;

        auto makeDelta = [](json tc) {
            json j;
            j["choices"] = json::array({{
                {"index", 0},
                {"delta", {{"tool_calls", json::array({tc})}}}
            }});
            return "data: " + j.dump() + "\n\n";
        };

        // chunk 1: id + name
        json tc1;
        tc1["index"] = 0;
        tc1["id"] = "call_abc";
        tc1["type"] = "function";
        tc1["function"] = {{"name", "read_file"}, {"arguments", ""}};

        // chunk 2: first argument fragment
        json tc2;
        tc2["index"] = 0;
        tc2["function"] = {{"arguments", "{\"path\":\"/f"}};

        // chunk 3: second argument fragment
        json tc3;
        tc3["index"] = 0;
        tc3["function"] = {{"arguments", "oo.txt\"}"}};

        // finish chunk
        json finish;
        finish["choices"] = json::array({{
            {"index", 0},
            {"delta", json::object()},
            {"finish_reason", "tool_calls"}
        }});
        finish["usage"] = {
            {"prompt_tokens", 10},
            {"completion_tokens", 5},
            {"total_tokens", 15}
        };

        std::string body = makeDelta(tc1) + makeDelta(tc2) + makeDelta(tc3) +
                          "data: " + finish.dump() + "\n\n"
                          "data: [DONE]\n\n";
        res.set_content(body, "text/event-stream");
    });
    server.start();

    LLMClient client(makeConfig(server.port));
    bool tool_started = false;
    LLMResponse response;

    StreamCallbacks cb;
    cb.on_tool_call_start = [&]() { tool_started = true; };
    cb.on_complete = [&](const LLMResponse& r) { response = r; };

    response = client.chat({{MessageRole::User, "Read a file"}}, {}, "", cb);

    CHECK(tool_started);
    CHECK(response.tool_calls.size() == 1);
    CHECK(response.tool_calls[0].id == "call_abc");
    CHECK(response.tool_calls[0].function_name == "read_file");
    CHECK(response.tool_calls[0].arguments == "{\"path\":\"/foo.txt\"}");
    CHECK(response.finish_reason == "tool_calls");

    server.stop();
    PASS();
}

// =========================================================================
// 测试 3: 非流式调用
// =========================================================================

void test_non_streaming() {
    TEST("chatNonStreaming() 完整 JSON 解析");
    MockServer server;

    const char* responseJson = R"({
        "id": "chatcmpl-123",
        "object": "chat.completion",
        "created": 1700000000,
        "model": "test-model",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "你好，我是 AI 助手。"
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 50,
            "completion_tokens": 30,
            "total_tokens": 80
        }
    })";

    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        res.set_content(responseJson, "application/json");
    });
    server.start();

    LLMClient client(makeConfig(server.port));
    auto response = client.chatNonStreaming(
        {{MessageRole::User, "Hello"}}, {}, "Be helpful.");

    CHECK(response.id == "chatcmpl-123");
    CHECK(response.content == "你好，我是 AI 助手。");
    CHECK(response.finish_reason == "stop");
    CHECK(response.prompt_tokens == 50);
    CHECK(response.completion_tokens == 30);
    CHECK(response.total_tokens == 80);

    server.stop();
    PASS();
}

// =========================================================================
// 测试 4: HTTP 401 错误
// =========================================================================

void test_http_401_error() {
    TEST("HTTP 401 错误 → 抛出 runtime_error");
    MockServer server;

    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        res.status = 401;
        res.set_content(R"({"error":{"message":"Invalid API Key","code":"invalid_api_key"}})",
                        "application/json");
    });
    server.start();

    LLMClient client(makeConfig(server.port));

    bool caught = false;
    try {
        client.chatNonStreaming({{MessageRole::User, "Hi"}});
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        CHECK(msg.find("invalid_api_key") != std::string::npos ||
              msg.find("Invalid API Key") != std::string::npos ||
              msg.find("API Key 无效") != std::string::npos);
    }
    CHECK(caught);

    server.stop();
    PASS();
}

// =========================================================================
// 测试 5: API Key 缺失
// =========================================================================

void test_missing_api_key() {
    TEST("API Key 为空 → validateConfig 抛异常");
    ProviderConfig cfg;
    cfg.base_url = "http://localhost:8080";
    cfg.model = "test";
    cfg.api_key = ""; // 故意为空

    LLMClient client(cfg);
    bool caught = false;
    try {
        client.chatNonStreaming({{MessageRole::User, "Hi"}});
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        CHECK(msg.find("API Key") != std::string::npos);
    }
    CHECK(caught);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_llm_client ===\n\n";

    test_streaming_token_callback();
    test_streaming_tool_call_callback();
    test_non_streaming();
    test_http_401_error();
    test_missing_api_key();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
