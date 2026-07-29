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

// 顶层/choice 字符串字段为显式 null 时不抛异常、回落空串。
//
// 回归背景：旧实现用 j.value() 读 id/model，遇显式 null 抛 type_error.302；
// 现与 Message.h::getStringOrDefault 语义统一（null → 默认值）。
void test_null_top_level_fields() {
    TEST("顶层 id/model/finish_reason 为显式 null → 回落空串不抛异常");
    SSEParser parser;

    StreamChunk received;
    bool got = false;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; got = true; });

    std::string error_msg;
    parser.setOnError([&](const std::string& e) { error_msg = e; });

    std::string sse = makeSseData(
        R"({"id":null,"model":null,)"
        R"("choices":[{"delta":{"content":"正常"},"finish_reason":null}]})");
    parser.feed(sse);

    CHECK(error_msg.empty());          // 不应触发错误回调
    CHECK(got);
    CHECK(received.id == "");          // null 回落空串
    CHECK(received.model == "");
    CHECK(received.finish_reason == "");
    CHECK(received.content_delta == "正常"); // 正常增量不受影响
    PASS();
}

// delta 内 content/reasoning_content 为显式 null 时回落空串。
//
// 回归背景：部分 provider 首个 role chunk 中 content 为显式 null。
void test_null_delta_content_fields() {
    TEST("delta.content/reasoning_content 为显式 null → 回落空串");
    SSEParser parser;

    StreamChunk received;
    bool got = false;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; got = true; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{"role":"assistant",)"
        R"("content":null,"reasoning_content":null}}]})");
    parser.feed(sse);

    CHECK(got);
    CHECK(received.content_delta == "");
    CHECK(received.reasoning_delta == "");
    PASS();
}

// tool_calls 内字符串字段为显式 null 时回落默认值，正常字段不受影响。
//
// 回归背景：旧实现用 tc.value()/func.value() 遇 null 抛 type_error.302。
void test_null_tool_call_fields() {
    TEST("tool_call 内 id/type/function.name 为显式 null → 回落默认值");
    SSEParser parser;

    StreamChunk received;
    bool got = false;
    parser.setOnChunk([&](const StreamChunk& c) { received = c; got = true; });

    std::string error_msg;
    parser.setOnError([&](const std::string& e) { error_msg = e; });

    std::string sse = makeSseData(
        R"({"choices":[{"delta":{"tool_calls":[)"
        R"({"index":0,"id":null,"type":null,)"
        R"("function":{"name":null,"arguments":"{\"k\":1}"}}]}}]})");
    parser.feed(sse);

    CHECK(error_msg.empty());
    CHECK(got);
    CHECK(received.tool_call_deltas.size() == 1);
    CHECK(received.tool_call_deltas[0].index == 0);
    CHECK(received.tool_call_deltas[0].id == "");              // null → 空串
    CHECK(received.tool_call_deltas[0].type == "function");    // null → 默认值
    CHECK(received.tool_call_deltas[0].function_name == "");   // null → 空串
    CHECK(received.tool_call_deltas[0].arguments == "{\"k\":1}"); // 正常字段不受影响
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
    test_null_top_level_fields();
    test_null_delta_content_fields();
    test_null_tool_call_fields();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
