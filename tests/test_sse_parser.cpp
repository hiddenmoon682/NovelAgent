#include "llm/SSEParser.h"
#include <cassert>
#include <iostream>

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

// 构造 SSE data: 行
static std::string makeSseData(const std::string& json) {
    return "data: " + json + "\n\n";
}

// =========================================================================

void test_single_token_delta() {
    TEST("单个 content delta");
    SSEParser parser;

    StreamChunk received;
    bool got = false;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; got = true; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{"content":"你好"}}]})");
    parser.feed(sse);

    CHECK(got);
    CHECK(received.content_delta == "你好");
    CHECK(received.tool_call_deltas.empty());
    CHECK(!received.is_end);
    PASS();
}

void test_tool_call_with_id_and_name() {
    TEST("tool_call delta 含 id 和 function.name");
    SSEParser parser;

    StreamChunk received;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{"tool_calls":[)"
        R"({"index":0,"id":"call_123","type":"function",)"
        R"("function":{"name":"write_chapter","arguments":""}}]}}]})");
    parser.feed(sse);

    CHECK(received.tool_call_deltas.size() == 1);
    CHECK(received.tool_call_deltas[0].index == 0);
    CHECK(received.tool_call_deltas[0].id == "call_123");
    CHECK(received.tool_call_deltas[0].function_name == "write_chapter");
    CHECK(received.tool_call_deltas[0].arguments == "");
    PASS();
}

void test_tool_call_arguments_fragment() {
    TEST("tool_call delta arguments 片段（无 id/name）");
    SSEParser parser;

    StreamChunk received;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{"tool_calls":[)"
        R"({"index":0,"function":{"arguments":"{\"chap"}}]}}]})");
    parser.feed(sse);

    CHECK(received.tool_call_deltas.size() == 1);
    CHECK(received.tool_call_deltas[0].index == 0);
    CHECK(received.tool_call_deltas[0].id == "");         // 无 id
    CHECK(received.tool_call_deltas[0].function_name == ""); // 无 name
    CHECK(received.tool_call_deltas[0].arguments == "{\"chap");
    PASS();
}

void test_multiple_tool_calls_in_chunk() {
    TEST("同一 chunk 含多个 index 的 tool_call");
    SSEParser parser;

    StreamChunk received;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{"tool_calls":[)"
        R"({"index":0,"id":"call_a","function":{"name":"f1","arguments":"{}"}},)"
        R"({"index":1,"id":"call_b","function":{"name":"f2","arguments":"[]"}})"
        R"(]}}]})");
    parser.feed(sse);

    CHECK(received.tool_call_deltas.size() == 2);
    CHECK(received.tool_call_deltas[0].index == 0);
    CHECK(received.tool_call_deltas[0].id == "call_a");
    CHECK(received.tool_call_deltas[0].function_name == "f1");
    CHECK(received.tool_call_deltas[1].index == 1);
    CHECK(received.tool_call_deltas[1].id == "call_b");
    CHECK(received.tool_call_deltas[1].function_name == "f2");
    PASS();
}

void test_incomplete_line_across_buffer() {
    TEST("跨 buffer 的不完整 SSE 行");
    SSEParser parser;

    StreamChunk received;
    bool got = false;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; got = true; });

    // 第一块：data 行被截断（不含 \n\n 结束符）
    parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"分");
    CHECK(!got); // 还没收到完整事件

    // 第二块：补全剩余数据和事件边界
    parser.feed("段\"}}]}\n\n");
    CHECK(got);
    CHECK(received.content_delta == "分段");
    PASS();
}

void test_multiple_data_lines() {
    TEST("多 data: 行合并（\\n 拼接后形成有效 JSON）");
    SSEParser parser;

    StreamChunk received;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; });

    // 两行 data: 用 \n 拼接后恰好构成有效 JSON
    std::string sse =
        "data: {\"choices\":[\n"
        "data: {\"delta\":{\"content\":\"hello\"}}]}\n\n";
    parser.feed(sse);

    CHECK(received.content_delta == "hello");
    PASS();
}

void test_done_signal() {
    TEST("[DONE] 终止信号 → is_end=true");
    SSEParser parser;

    StreamChunk received;
    bool got = false;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; got = true; });

    parser.feed("data: [DONE]\n\n");

    CHECK(got);
    CHECK(received.is_end);
    CHECK(received.content_delta.empty());
    PASS();
}

void test_json_parse_error() {
    TEST("JSON 解析错误 → onError 回调");
    SSEParser parser;

    std::string error_msg;
    parser.setOnError([&](const std::string& e) { error_msg = e; });

    bool chunk_received = false;
    parser.setOnChunk([&](const StreamChunk&) { chunk_received = true; });

    parser.feed("data: {invalid json}\n\n");

    CHECK(!error_msg.empty());
    CHECK(!chunk_received);
    PASS();
}

void test_finish_reason_in_chunk() {
    TEST("finish_reason 出现在 chunk 中");
    SSEParser parser;

    StreamChunk received;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{},"finish_reason":"stop"}]})");
    parser.feed(sse);

    CHECK(received.finish_reason == "stop");
    PASS();
}

void test_reset_clears_buffer() {
    TEST("reset() 清空内部缓冲");
    SSEParser parser;

    // 喂入不完整数据
    parser.feed("data: {\"incomplete");
    parser.reset();
    parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"新\"}}]}\n\n");

    StreamChunk received;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; });

    // 喂入完整事件
    parser.feed("data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n\n");
    CHECK(received.content_delta == "好"); // "新" 被 reset 丢弃
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_sse_parser ===\n\n";

    test_single_token_delta();
    test_tool_call_with_id_and_name();
    test_tool_call_arguments_fragment();
    test_multiple_tool_calls_in_chunk();
    test_incomplete_line_across_buffer();
    test_multiple_data_lines();
    test_done_signal();
    test_json_parse_error();
    test_finish_reason_in_chunk();
    test_reset_clears_buffer();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
