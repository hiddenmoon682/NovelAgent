#include "agent/core/Agent.h"
#include "agent/tool/ToolRegistry.h"
#include "agent/session/SessionPersistence.h"
#include "agent/context/Memory.h"
#include "agent/context/TokenBudget.h"
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
#include <set>
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

// 校验消息序列符合 OpenAI 协议（与 test_core_loop.cpp 同构）：tool 消息必须
// 紧随携带匹配 tool_call_id 的 assistant 消息，且每个 tool_call 在下一条
// 非 tool 消息前都有结果。
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
    TEST("B2 — processUserMessage 后会话增量落盘到 sessions/<id>.json");

    // 准备一个临时项目目录（先清理残留，避免跨运行干扰断言）
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

    // 处理前：多会话索引尚未创建（首次 save 时才初始化）
    const std::string indexPath = tmp + "/.novelagent/sessions/index.json";
    CHECK(!utils::file::exists(indexPath));

    auto response = agent.process("帮我写第二章开头");
    CHECK(response.content == "第二章开头");

    // 处理后：索引已创建，active 会话文件含本轮 user + assistant 两条消息
    json index = json::parse(utils::file::readText(indexPath));
    const std::string activeId = index["active"].get<std::string>();
    CHECK(!activeId.empty());

    json after = json::parse(utils::file::readText(
        tmp + "/.novelagent/sessions/" + activeId + ".json"));
    CHECK(after.is_array());
    CHECK(after.size() == 2);
    CHECK(after[0]["role"] == "user");
    CHECK(after[0]["content"] == "帮我写第二章开头");
    CHECK(after[1]["role"] == "assistant");

    // token 用量缓存应已刷新（供 StatusBar 展示）
    CHECK(agent.contextUsage().total_tokens > 0);

    server.stop();
    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// 测试: 轮内累积溢出 — Agent 层端到端优雅终止
// =========================================================================

void test_context_overflow_end_to_end() {
    TEST("context_overflow — 轮内溢出压缩不足时端到端优雅终止");

    // 场景：极小 token 预算 + 大体积工具结果（≈4550 token），使
    // on_tool_results_applied 复评（评估→压缩→复评）仍为 Error 级：
    // 压缩只能吃掉旧历史，本轮 tool_result 留在保留窗口内，怎么压都超限。
    MockServer server;
    int stream_calls = 0;      // 主循环 chat() 轮次（stream=true）
    int nonstream_calls = 0;   // Compactor 摘要调用（stream=false）
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            ++stream_calls;
            if (stream_calls <= 2) {
                // 前两轮小对话：制造可压缩的旧历史
                std::string body = llm::test::sseContentChunk("收到") +
                                   llm::test::sseFinishChunk("stop") +
                                   llm::test::sseDone;
                res.set_content(body, "text/event-stream");
            } else if (stream_calls == 3) {
                // 第三轮：调用大结果工具，触发轮内累积溢出
                std::string body =
                    llm::test::sseToolCallChunk(0, "call_big", "read_chapter", "{}") +
                    llm::test::sseFinishChunk("tool_calls") +
                    llm::test::sseDone;
                res.set_content(body, "text/event-stream");
            } else {
                // 优雅终止后不应再发起下一轮 —— 若到达此分支即为缺陷
                std::string body = llm::test::sseContentChunk("不应到达") +
                                   llm::test::sseFinishChunk("stop") +
                                   llm::test::sseDone;
                res.set_content(body, "text/event-stream");
            }
        } else {
            // Compactor 的摘要请求走非流式路径，与主循环轮次分开计数
            ++nonstream_calls;
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;

    // 大结果工具：约 3500 个英文单词 ≈ 4550 token（TokenCounter 按单词×1.3 估算）。
    // 用 read_chapter 命名：它在渐进式加载的核心工具集中（否则执行被拦截
    // 只返回引导错误），且超长章节读取正是轮内溢出的真实来源。
    // 注意控制在 ToolPipeline 的 22000 字符 content 截断阈值以内。
    registry.registerTool(
        "read_chapter", "读取章节",
        utils::schema::object({}),
        agent::ToolCategory::Content,
        [](const json&) -> json {
            std::string big;
            big.reserve(21000);
            for (int i = 0; i < 3500; ++i) big += "lorem ";
            return {{"content", big}};
        }
    );

    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);

    // 预算 600：warm-up 轮次远低于 80% AutoCompact 阈值（发送前评估不干扰），
    // 大结果回填后无论怎么压缩都超 model_limit → 复评 Error → hook 返回 false
    agent::TokenBudget budget;
    budget.model_limit = 600;
    agent.setTokenBudget(budget);

    // 两轮 warm-up：确保压缩切割点落在旧历史，本轮 user 输入留在保留窗口
    agent.process("历史对话一");
    agent.process("历史对话二");
    CHECK(agent.memory().size() == 4);

    auto response = agent.process("读取大文件");

    // 1) budget_exhausted 映射为 finish_reason="context_overflow"
    CHECK(response.finish_reason == "context_overflow");

    // 2) memory 未被快照回滚：用户输入与工具结果仍在会话历史中
    const auto& msgs = agent.memory().all();
    bool user_input_kept = false;
    bool tool_result_kept = false;
    for (const auto& m : msgs) {
        if (m.role == llm::MessageRole::User && m.content == "读取大文件")
            user_input_kept = true;
        if (m.role == llm::MessageRole::Tool &&
            m.content.find("lorem") != std::string::npos)
            tool_result_kept = true;
    }
    CHECK(user_input_kept);
    CHECK(tool_result_kept);

    // 3) 终止后未发起下一轮 LLM 调用：主循环恰好 3 次流式请求
    //    （2 次 warm-up + 1 次 tool_calls），第 4 次的"不应到达"分支从未触发；
    //    唯一的非流式请求是 hook 内的压缩摘要，证明压缩确实先于终止发生
    CHECK(stream_calls == 3);
    CHECK(nonstream_calls == 1);

    // 4) 最终消息序列协议合法：无孤儿 tool 消息、无未配对 tool_calls
    CHECK(isLegalMessageSequence(msgs));

    // 优雅终止走正常 return 路径，Agent 状态应恢复可接受输入
    CHECK(agent.canAcceptInput());

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
    test_session_persisted_after_message();
    test_exception_recovery();
    test_context_overflow_end_to_end();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
