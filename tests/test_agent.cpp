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

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
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
    agent::Agent agent(factory, registry);

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
    agent::Agent agent(factory, registry);
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
    agent::Agent agent(factory, registry);

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
    agent::Agent agent(factory, registry);

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
    agent::Agent agent(factory, registry);

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
    // 路径来自系统临时目录，避免硬编码仓库绝对路径导致盘符绑定
    const std::string tmp =
        (std::filesystem::temp_directory_path() / "tmp_test_b2_persist").string();
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
    agent::Agent agent(factory, registry);

    // 绑定 SessionPersistence，使 processUserMessage 末尾的 saveSessionState 真正落盘
    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);
    agent.setPersistence(&persistence);

    // 处理前：多会话索引尚未创建（首次 save 时才初始化）
    const std::string indexPath = tmp + "/.novelagent/sessions/index.json";
    CHECK(!utils::file::exists(indexPath));

    auto response = agent.process("帮我写第二章开头");
    CHECK(response.content == "第二章开头");

    // 处理后：当前池会话按 id 落盘（D3），会话文件含本轮 user + assistant 两条消息
    CHECK(!agent.sessionIds().empty());
    const std::string sid = agent.sessionIds().front();
    CHECK(utils::file::exists(tmp + "/.novelagent/sessions/" + sid + ".json"));

    json after = json::parse(utils::file::readText(
        tmp + "/.novelagent/sessions/" + sid + ".json"));
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
// 测试: 延迟创建 — 经 Agent.process 端到端链路：newSession 不落盘，首条消息才创建会话
// =========================================================================

void test_lazy_creation_via_process() {
    TEST("延迟创建 — process 端到端：newSession 不落盘，首条消息才创建会话");

    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "tmp_test_lazy_process").string();
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "延迟创建测试");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("回复"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry);

    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);
    agent.setPersistence(&persistence);

    const std::string indexPath = tmp + "/.novelagent/sessions/index.json";

    // 第一条消息：当前池会话按 id 落盘（方案 C：首条消息才写文件）
    auto r1 = agent.process("第一条消息");
    CHECK(r1.content == "回复");
    CHECK(!agent.pendingNewSession());
    CHECK(agent.sessionIds().size() == 1);
    const std::string a_id = agent.sessionIds().front();
    CHECK(utils::file::exists(tmp + "/.novelagent/sessions/" + a_id + ".json"));

    // 新建会话：新池会话在池中但未落盘（pending）
    agent.newSession();
    CHECK(agent.pendingNewSession());
    CHECK(agent.sessionIds().size() == 2);

    // 首条消息 → 落盘为新会话 B（含 user + assistant 两条）
    auto r2 = agent.process("新会话的第一条消息");
    CHECK(r2.content == "回复");
    CHECK(!agent.pendingNewSession());
    CHECK(agent.sessionIds().size() == 2);
    const std::string b_id = agent.sessionIds().back();
    CHECK(b_id != a_id);
    CHECK(utils::file::exists(tmp + "/.novelagent/sessions/" + b_id + ".json"));
    json b = json::parse(utils::file::readText(
        tmp + "/.novelagent/sessions/" + b_id + ".json"));
    CHECK(b.is_array());
    CHECK(b.size() == 2);
    CHECK(b[0]["role"] == "user");
    CHECK(b[0]["content"] == "新会话的第一条消息");
    CHECK(b[1]["role"] == "assistant");

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
    agent::Agent agent(factory, registry);

    // 预算 600：warm-up 轮次远低于 80% AutoCompact 阈值（发送前评估不干扰），
    // 大结果回填后无论怎么压缩都超 model_limit → 复评 Error → hook 返回 false
    agent.setModelLimit(600);

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
// 测试: 历史层归档链路 — Agent 压缩后按会话 id 归档被压缩消息
// =========================================================================

void test_history_sink_wiring() {
    TEST("历史层归档 — 压缩后按会话 id 归档被压缩消息到完整历史层");

    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "tmp_test_history_sink").string();
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "历史归档测试");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            // Compactor 摘要请求走非流式路径
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry);

    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);
    agent.setPersistence(&persistence);

    // 生成 8 轮对话 = 16 条消息（> keep_exchanges=5 的 10 条保留窗口，可压缩）
    for (int i = 0; i < 8; ++i) agent.process("第 " + std::to_string(i) + " 条消息");
    CHECK(agent.memory().size() == 16);

    // 手动压缩：触发 applyCompaction → runtime 直接 appendHistory（D4，无 sink）
    auto cr = agent.compactConversation();
    CHECK(cr.messages_compacted > 0);

    // 完整历史层已按会话 id 落盘：<sid>.history 含被压缩消息（D4 直接落盘验证）
    const std::string sid = agent.sessionIds().front();
    auto history = persistence.loadHistory(sid);
    CHECK(history.size() == static_cast<size_t>(cr.messages_compacted));

    // 空消息不被归档：清空后无可压缩内容，compactConversation 返回 0 且不触发
    // 归档（applyCompaction 仅在 compacted 非空时落盘）。
    agent.clearMemory();
    const size_t history_before = persistence.loadHistory(sid).size();
    auto cr2 = agent.compactConversation();
    CHECK(cr2.messages_compacted == 0);
    CHECK(persistence.loadHistory(sid).size() == history_before);

    server.stop();
    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// 测试: 多会话并行池 — 每个会话独立 SessionRuntime，可并行/独立对话
// =========================================================================

void test_multi_session_pool() {
    TEST("多会话池 — createSession 独立运行时，process(session_id) 独立对话");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry);

    // 建两个会话：各自独立 SessionRuntime
    const std::string sid1 = agent.createSession();
    const std::string sid2 = agent.createSession();
    CHECK(sid1 != sid2);
    CHECK(agent.sessionIds().size() == 2);

    // 各自 process，独立回复
    auto r1 = agent.process(sid1, "消息一");
    auto r2 = agent.process(sid2, "消息二");
    CHECK(r1.content == "回复");
    CHECK(r2.content == "回复");

    // 会话内存独立：各自含 user+assistant 两条，不互相污染
    CHECK(agent.session(sid1)->memory().size() == 2);
    CHECK(agent.session(sid2)->memory().size() == 2);
    CHECK(agent.session(sid1)->memory().messages()[0].content == "消息一");
    CHECK(agent.session(sid2)->memory().messages()[0].content == "消息二");

    // 不存在的会话 → session_not_found，不抛异常
    auto rn = agent.process("s-nonexistent", "hi");
    CHECK(rn.finish_reason == "session_not_found");

    // 删除会话 → 池里移除
    CHECK(agent.deleteSessionRuntime(sid1));
    CHECK(agent.sessionIds().size() == 1);
    CHECK(agent.session(sid2) != nullptr);

    server.stop();
    PASS();
}

// =========================================================================
// 测试: 多会话持久化 — 按 session_id 落盘 CS 恢复
// =========================================================================

void test_multi_session_persistence() {
    TEST("多会话持久化 — process 后按 session_id 落盘，重启可恢复");

    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "tmp_test_multi_persist").string();
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "多会话持久化测试");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry);
    agent.setPersistence(&persistence);

    // 建会话并 process → 按 id 落盘
    const std::string sid1 = agent.createSession();
    agent.process(sid1, "持久化消息");
    CHECK(agent.session(sid1)->persisted());
    CHECK(utils::file::exists(tmp + "/.novelagent/sessions/" + sid1 + ".json"));

    // 用持久化层按 id 直接读回：内容一致
    auto loaded = persistence.load(sid1);
    CHECK(loaded.messages().size() == 2);
    CHECK(loaded.messages()[0].content == "持久化消息");

    // 恢复路径：新建一个能定位到同一 id 的 runtime，loadSessionState 恢复（P5：deps 构造注入）
    agent::SessionRuntime rt(sid1, factory, registry, agent::SessionRuntimeDeps{
        .persistence = &persistence,
    });
    rt.loadSessionState();
    CHECK(rt.memory().messages().size() == 2);
    CHECK(rt.memory().messages()[0].content == "持久化消息");

    server.stop();
    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// 测试: 多会话并行 — 共享线程池上两个会话并发执行
// =========================================================================

void test_parallel_sessions() {
    TEST("多会话并行 — 两个会话并发 process 各自独立完成");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    llm::Memory memory;
    agent::Agent agent(factory, registry);

    const std::string sid1 = agent.createSession();
    const std::string sid2 = agent.createSession();

    // 两个线程并发提交两个会话的 process（共享线程池 4 线程，可并行）
    std::thread t1([&] {
        auto r = agent.process(sid1, "并行消息一");
        CHECK(r.content == "回复");
    });
    std::thread t2([&] {
        auto r = agent.process(sid2, "并行消息二");
        CHECK(r.content == "回复");
    });
    t1.join();
    t2.join();

    // 两会话内存各自独立、互不污染
    CHECK(agent.session(sid1)->memory().messages()[0].content == "并行消息一");
    CHECK(agent.session(sid2)->memory().messages()[0].content == "并行消息二");

    server.stop();
    PASS();
}

// =========================================================================
// 测试: 历史会话懒物化（P8）— 启动不物化，点开历史会话才物化并恢复历史
// =========================================================================

void test_materialize_history_session() {
    TEST("历史会话懒物化 — 启动不物化，materializeSession 恢复历史，不存在 id 拒绝");

    const std::string tmp = (std::filesystem::temp_directory_path() /
                             "tmp_test_materialize").string();
    if (utils::file::exists(tmp)) utils::file::removeDir(tmp);
    ProjectIO::createProjectDir(tmp, "物化测试");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    FileStorageBackend storage(tmp);
    agent::SessionPersistence persistence(storage);

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;

    // 第一轮：建会话 + process 落盘一条历史
    {
        agent::Agent agent(factory, registry);
        agent.setPersistence(&persistence);
        const std::string sid = agent.createSession();
        agent.process(sid, "历史消息");
        CHECK(agent.session(sid)->persisted());
    }  // Agent A 销毁（模拟应用退出）

    // 第二轮：新 Agent（模拟重启）。启动不物化任何会话（P8 懒物化）。
    agent::Agent agent(factory, registry);
    agent.setPersistence(&persistence);
    CHECK(agent.sessionIds().empty());  // 启动未物化（未建 runtime/client）

    // 物化历史会话：持久层存在 → 建 runtime + 恢复历史消息
    const std::string sid = persistence.listSessions().front().id;
    CHECK(agent.materializeSession(sid));
    CHECK(agent.session(sid) != nullptr);
    CHECK(agent.session(sid)->memory().messages().size() == 2);
    CHECK(agent.session(sid)->memory().messages()[0].content == "历史消息");

    // 不存在的历史 id → 物化拒绝（返回 false，不建幽灵 runtime）
    CHECK(!agent.materializeSession("s-never-existed"));
    CHECK(agent.session(sid) != nullptr);  // 既有物化会话不受影响

    server.stop();
    utils::file::removeDir(tmp);
    PASS();
}

// =========================================================================
// 测试: E1 — 会话创建时经 system_prompt_provider 重建 prompt
// =========================================================================

void test_system_prompt_provider_rebuild() {
    TEST("E1 — 会话创建经 provider 重建 prompt，provider 为空回退注入值");

    MockServer server;
    server.start();
    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;

    // provider 非空：prompt = provider 结果（读到最新技能目录）
    {
        agent::SessionRuntimeDeps deps;
        deps.config.system_prompt_provider = [] { return "动态prompt"; };
        deps.system_prompt = "静态prompt";
        agent::SessionRuntime rt("s-provider", factory, registry, std::move(deps));
        CHECK(rt.memory().systemPrompt() == "动态prompt");
    }
    // provider 为空：回退构造时注入的 system_prompt 兜底
    {
        agent::SessionRuntimeDeps deps;
        deps.system_prompt = "静态prompt";
        agent::SessionRuntime rt("s-fallback", factory, registry, std::move(deps));
        CHECK(rt.memory().systemPrompt() == "静态prompt");
    }
    // provider 抛异常：回退注入值，不崩溃
    {
        agent::SessionRuntimeDeps deps;
        deps.config.system_prompt_provider = []() -> std::string { throw std::runtime_error("boom"); };
        deps.system_prompt = "兜底prompt";
        agent::SessionRuntime rt("s-throw", factory, registry, std::move(deps));
        CHECK(rt.memory().systemPrompt() == "兜底prompt");
    }

    server.stop();
    PASS();
}

// =========================================================================
// 测试: P9 — 并发上限满时第 5 个会话 process 返回 concurrency_full（不排队）
// =========================================================================

void test_concurrency_full() {
    TEST("P9 — 并发满时第 5 个 process 返回 concurrency_full");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        // 慢响应：挂起以保持 in-flight 满（4 个并发会话都占着池线程）
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("【摘要】旧历史已压缩"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(factory, registry);

    // 提交 4 个并发慢会话（异步，占满共享线程池 4 线程）
    std::vector<std::string> sids;
    for (int i = 0; i < 4; ++i) sids.push_back(agent.createSession());
    for (auto& s : sids) {
        agent.submitProcess(s, "并行消息", {},
                            [](const std::string&, llm::LLMResponse) {});
    }

    // 第 5 个同步 process：canSubmit() 检测 in-flight 满 → concurrency_full，不排队
    auto r = agent.process(agent.createSession(), "第五个");
    CHECK(r.finish_reason == "concurrency_full");

    // 等 4 个慢任务（2s）自然完成后再 stop，避免 stop 后连接失败重试拖慢 agent 析构 join
    std::this_thread::sleep_for(std::chrono::seconds(3));
    server.stop();
    PASS();
}

// =========================================================================
// 测试: 方案 A — shutdown 取消并等待所有 in-flight 任务退场（防析构 UAF）
// =========================================================================

void test_shutdown_waits_inflight() {
    TEST("A — shutdown 等待所有 in-flight 任务退场（on_complete 全部执行）");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        // 慢响应：模拟 LLM 长生成，留给 shutdown 取消的机会
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("摘要"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(factory, registry);

    std::atomic<int> completed{0};
    for (int i = 0; i < 2; ++i) {
        const std::string sid = agent.createSession();
        agent.submitProcess(sid, "消息", {},
                            [&completed](const std::string&, llm::LLMResponse) {
                                completed.fetch_add(1);
                            });
    }

    // 立即 shutdown：取消所有 in-flight 并等待它们退场
    auto t0 = std::chrono::steady_clock::now();
    agent.shutdown();
    auto t1 = std::chrono::steady_clock::now();

    // shutdown 返回时所有任务必须已完成（on_complete 全部执行，无残留）
    CHECK(completed.load() == 2);
    // 等待退场时间应远小于任务自然串行总和（2×200ms 并行 → 上限给宽裕值；
    // 若实现误串行等满各自超时则会 >4s，此上限足以区分）
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    CHECK(ms < 1500);

    server.stop();
    PASS();
}

// =========================================================================
// 测试: 方案 A — anyRunning 聚合信号（任一会话运行即 true，全空闲即 false）
// =========================================================================

void test_any_running() {
    TEST("A — anyRunning 聚合多次会话运行状态");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        // 慢响应：确保任务在断言期间处于 running 状态
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("摘要"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(factory, registry);

    // 空闲期：全空闲 → anyRunning 应为 false
    agent.createSession();
    agent.createSession();
    CHECK(!agent.anyRunning());

    // 提交会话 A 的慢任务（异步），轮询等待其进入 running
    std::atomic<int> completed{0};
    const std::string sidA = agent.createSession();
    agent.submitProcess(sidA, "消息", {},
                        [&completed](const std::string&, llm::LLMResponse) {
                            completed.fetch_add(1);
                        });
    bool saw_running = false;
    for (int i = 0; i < 100 && !saw_running; ++i) {
        saw_running = agent.anyRunning();
        if (!saw_running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(saw_running);

    // 等任务完成退场 → 全空闲 → anyRunning 复位
    for (int i = 0; i < 200 && agent.anyRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(completed.load() == 1);
    CHECK(!agent.anyRunning());

    server.stop();
    PASS();
}

// =========================================================================
// 测试: 方案 A — 并发提交上限（检查+占用原子，防 TOCTOU 突破 kMaxConcurrent）
// =========================================================================

void test_concurrent_submit_cap() {
    TEST("A — 并发提交不突破并发上限（accepted ≤ 4）");

    MockServer server;
    server.svr.Post("/v1/chat/completions", [&](const httplib::Request& req,
                                                 httplib::Response& res) {
        // 慢响应：让被接受的任务充分停留 in-flight，暴露并发上限
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        if (isStreamRequest(req)) {
            std::string body = llm::test::sseContentChunk("回复") +
                               llm::test::sseFinishChunk("stop") +
                               llm::test::sseDone;
            res.set_content(body, "text/event-stream");
        } else {
            res.set_content(nonStreamJson("摘要"), "application/json");
        }
    });
    server.start();

    llm::LLMClientFactory factory(makeConfig(server.port));
    agent::ToolRegistry registry;
    agent::Agent agent(factory, registry);

    std::atomic<int> accepted{0};
    std::vector<std::string> sids;
    for (int i = 0; i < 8; ++i) sids.push_back(agent.createSession());

    // 8 线程同时并发提交，争抢并发上限（4 线程池）
    std::vector<std::thread> threads;
    for (auto& s : sids) {
        threads.emplace_back([&, s]() {
            agent.submitProcess(s, "消息", {},
                                [&accepted](const std::string&, llm::LLMResponse r) {
                                    if (r.finish_reason != "concurrency_full")
                                        accepted.fetch_add(1);
                                });
        });
    }
    for (auto& t : threads) t.join();

    // 等所有被接受任务完成退场（600ms 慢任务）
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 原子占用保证：被接受（非拒绝）的任务数不得超过并发上限
    CHECK(accepted.load() <= 4);

    server.stop();
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_agent ===\n\n";

    test_simple_conversation();
    test_tool_call_loop();
    test_conversation_management();
    test_empty_input();
    test_session_persisted_after_message();
    test_lazy_creation_via_process();
    test_exception_recovery();
    test_context_overflow_end_to_end();
    test_history_sink_wiring();
    test_multi_session_pool();
    test_multi_session_persistence();
    test_parallel_sessions();
    test_materialize_history_session();
    test_system_prompt_provider_rebuild();
    test_concurrency_full();
    test_shutdown_waits_inflight();
    test_any_running();
    test_concurrent_submit_cap();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
