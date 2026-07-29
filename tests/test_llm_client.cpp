#include "llm/LLMClient.h"
#include "llm/HttpClient.h"
#include "test_sse_helpers.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
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
    cfg.max_context_tokens = 65536;
    return cfg;
}

// ── SSE 响应构造已抽取到 test_sse_helpers.h ──
using llm::test::sseContentChunk;
using llm::test::sseFinishChunk;
using llm::test::sseDone;
using llm::test::sseToolCallChunk;

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

    response = client.chat({Message::user("Hi")}, {}, "You are helpful.", cb);

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
        std::string body =
            sseToolCallChunk(0, "call_abc", "read_file", "") +           // id + name
            sseToolCallChunk(0, "", "", R"({"path":"/f)") +             // arguments 片段 1
            sseToolCallChunk(0, "", "", R"(oo.txt"})") +                // arguments 片段 2
            sseFinishChunk("tool_calls") +
            sseDone;
        res.set_content(body, "text/event-stream");
    });
    server.start();

    LLMClient client(makeConfig(server.port));
    bool tool_started = false;
    LLMResponse response;

    StreamCallbacks cb;
    cb.on_tool_call_start = [&]() { tool_started = true; };
    cb.on_complete = [&](const LLMResponse& r) { response = r; };

    response = client.chat({Message::user("Read a file")}, {}, "", cb);

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
        {Message::user("Hello")}, {}, "Be helpful.");

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
        client.chatNonStreaming({Message::user("Hi")});
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
        client.chatNonStreaming({Message::user("Hi")});
    } catch (const std::runtime_error& e) {
        caught = true;
        std::string msg = e.what();
        CHECK(msg.find("API Key") != std::string::npos);
    }
    CHECK(caught);
    PASS();
}

// =========================================================================
// 测试 6: Message 序列化 round-trip（assistant + tool_calls → content=null）
// =========================================================================

// 验证带 tool_calls 的 assistant 消息 to_json 后能被 from_json 读回：
// to_json 按 OpenAI 协议对空 content 输出 null，from_json 必须对 null 容错，
// 否则序列化往返直接抛 nlohmann type_error。
void test_message_tool_calls_roundtrip() {
    TEST("Message round-trip: assistant+tool_calls 的 content=null 容错");

    Message m;
    m.role = MessageRole::Assistant;
    m.content = "";  // 空 content + 非空 tool_calls → to_json 输出 null
    ToolCall tc;
    tc.id = "call_1";
    tc.type = "function";
    tc.function_name = "read_file";
    tc.arguments = R"({"path":"a.txt"})";
    m.tool_calls.push_back(tc);

    nlohmann::json j = m;
    CHECK(j["content"].is_null());  // 线上协议行为不变：仍输出 null

    Message m2;
    try {
        m2 = j.get<Message>();
    } catch (const std::exception& e) {
        FAIL(std::string("round-trip 抛异常: ") + e.what());
    }
    CHECK(m2.role == MessageRole::Assistant);
    CHECK(m2.content.empty());
    CHECK(m2.tool_calls.size() == 1);
    CHECK(m2.tool_calls[0].id == "call_1");
    CHECK(m2.tool_calls[0].function_name == "read_file");
    CHECK(m2.tool_calls[0].arguments == R"({"path":"a.txt"})");

    PASS();
}

// =========================================================================
// 测试 7: from_json 对各字符串字段显式 null 的容错
// =========================================================================

// 验证外部 JSON（如第三方 API 响应）中 content/reasoning_content/
// tool_call_id/name 为显式 null 时 from_json 不抛异常，回落为空串。
void test_message_from_json_null_fields() {
    TEST("Message from_json: 各字符串字段为 null 时容错回落空串");

    nlohmann::json j = {
        {"role", "assistant"},
        {"content", nullptr},
        {"reasoning_content", nullptr},
        {"tool_call_id", nullptr},
        {"name", nullptr}
    };

    Message m;
    try {
        m = j.get<Message>();
    } catch (const std::exception& e) {
        FAIL(std::string("null 字段导致异常: ") + e.what());
    }
    CHECK(m.content.empty());
    CHECK(m.reasoning_content.empty());
    CHECK(m.tool_call_id.empty());
    CHECK(m.name.empty());

    PASS();
}

// =========================================================================
// 测试 8: 非流式响应 content=null + tool_calls（真实 API 行为）
// =========================================================================

// 验证 chatNonStreaming 能解析 OpenAI/DeepSeek 对工具调用响应的标准格式
// （message.content 为 null），LLMResponse::from_json 不得抛异常。
void test_non_streaming_null_content_with_tool_calls() {
    TEST("chatNonStreaming: content=null + tool_calls 响应解析");
    MockServer server;

    const char* responseJson = R"({
        "id": "chatcmpl-tc",
        "object": "chat.completion",
        "created": 1700000000,
        "model": "test-model",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": null,
                "tool_calls": [{
                    "id": "call_x",
                    "type": "function",
                    "function": {"name": "read_file", "arguments": "{}"}
                }]
            },
            "finish_reason": "tool_calls"
        }],
        "usage": {"prompt_tokens": 5, "completion_tokens": 3, "total_tokens": 8}
    })";

    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        res.set_content(responseJson, "application/json");
    });
    server.start();

    LLMClient client(makeConfig(server.port));
    LLMResponse response;
    try {
        response = client.chatNonStreaming({Message::user("Read a file")});
    } catch (const std::exception& e) {
        server.stop();
        FAIL(std::string("content=null 响应解析抛异常: ") + e.what());
    }

    CHECK(response.content.empty());
    CHECK(response.tool_calls.size() == 1);
    CHECK(response.tool_calls[0].id == "call_x");
    CHECK(response.finish_reason == "tool_calls");

    server.stop();
    PASS();
}

// =========================================================================
// 测试 9: postStreaming 半途断流后不得重试（防止增量重复交付）
// =========================================================================

// 验证已向下游交付过 SSE 增量后遇可重试网络错误（Read）时不再整体重试：
// 服务端每次都发送一个增量后断开连接，若客户端重试会导致同一增量被
// 重复喂给下游。期望：仅 1 次请求、增量只交付 1 次、返回错误。
void test_streaming_no_retry_after_partial_delivery() {
    TEST("postStreaming: 已交付增量后断流 → 不重试、增量不重复");
    MockServer server;

    std::atomic<int> request_count{0};
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        ++request_count;
        res.set_chunked_content_provider(
            "text/event-stream",
            [](size_t offset, httplib::DataSink& sink) {
                if (offset == 0) {
                    std::string chunk = sseContentChunk("Hello");
                    sink.write(chunk.data(), chunk.size());
                    return true;
                }
                return false;  // 半途断流（连接异常关闭 → 客户端 Read 错误）
            });
    });
    server.start();

    HttpConfig cfg;
    cfg.base_url = "http://localhost:" + std::to_string(server.port);
    cfg.api_key = "test-key";
    cfg.max_retries = 2;
    cfg.retry_base_delay_ms = 10;
    HttpClient http(cfg);

    std::string received;
    auto res = http.postStreaming(
        "/v1/chat/completions", R"({"stream":true})",
        [&](const char* data, size_t len) {
            received.append(data, len);
            return true;
        });

    // 交付过增量后必须直接报错返回，不得重试
    CHECK(!res);
    CHECK(request_count.load() == 1);

    // "Hello" 只应出现一次（重试会导致重复交付）
    size_t count = 0;
    for (size_t pos = 0; (pos = received.find("Hello", pos)) != std::string::npos; ++pos)
        ++count;
    CHECK(count == 1);

    server.stop();
    PASS();
}

// =========================================================================
// 测试 10: postStreaming 未交付任何字节时仍可正常重试（守护用例）
// =========================================================================

// 验证修复不误伤正常重试路径：首次请求在写出任何字节前断开（下游未收到
// 数据），第二次请求返回完整 SSE 流。期望：重试成功、共 2 次请求。
void test_streaming_retry_when_nothing_delivered() {
    TEST("postStreaming: 未交付任何字节 → 仍可重试成功");
    MockServer server;

    std::atomic<int> request_count{0};
    std::string full_stream =
        sseContentChunk("World") + sseFinishChunk("stop") + sseDone;

    server.svr.Post("/v1/chat/completions", [&](const httplib::Request&,
                                                 httplib::Response& res) {
        int n = ++request_count;
        if (n == 1) {
            // 一个字节都未写出即断开 → 客户端 Read 错误且未交付任何数据
            res.set_chunked_content_provider(
                "text/event-stream",
                [](size_t, httplib::DataSink&) { return false; });
        } else {
            res.set_content(full_stream, "text/event-stream");
        }
    });
    server.start();

    HttpConfig cfg;
    cfg.base_url = "http://localhost:" + std::to_string(server.port);
    cfg.api_key = "test-key";
    cfg.max_retries = 2;
    cfg.retry_base_delay_ms = 10;
    HttpClient http(cfg);

    std::string received;
    auto res = http.postStreaming(
        "/v1/chat/completions", R"({"stream":true})",
        [&](const char* data, size_t len) {
            received.append(data, len);
            return true;
        });

    CHECK(res && res->status == 200);
    CHECK(request_count.load() == 2);
    CHECK(received.find("World") != std::string::npos);

    server.stop();
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
    test_message_tool_calls_roundtrip();
    test_message_from_json_null_fields();
    test_non_streaming_null_content_with_tool_calls();
    test_streaming_no_retry_after_partial_delivery();
    test_streaming_retry_when_nothing_delivered();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
