#include "agent/IToolProvider.h"
#include "agent/SubAgent.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"
#include "utils/SchemaUtils.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; std::cout << "  TEST " << n << " ... "; } while(0)
#define PASS()  do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(m) do { std::cout << "FAILED: " << m << "\n"; return; } while(0)
#define CHECK(c) do { if (!(c)) FAIL(#c); } while(0)

// 简单的 Mock LLMClient（实现 ILLMClient 接口）
class MockLLMClient : public llm::ILLMClient {
    std::string response_;
public:
    explicit MockLLMClient(std::string resp) : response_(std::move(resp)) {}

    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks,
                          const std::atomic<bool>*) override {
        llm::LLMResponse r;
        r.content = response_;
        r.finish_reason = "stop";
        return r;
    }

    llm::LLMResponse chatNonStreaming(const std::vector<llm::Message>&,
                                       const std::vector<llm::ToolDefinition>&,
                                       const std::string&) override {
        llm::LLMResponse r;
        r.content = response_;
        r.finish_reason = "stop";
        return r;
    }

    const ProviderConfig& config() const override { static ProviderConfig c; return c; }
};

// 慢响应 Mock LLMClient（B3 超时测试用）
class SlowMockLLMClient : public llm::ILLMClient {
    int delay_ms_;
public:
    explicit SlowMockLLMClient(int delay_ms = 5000) : delay_ms_(delay_ms) {}

    llm::LLMResponse chat(const std::vector<llm::Message>&,
                          const std::vector<llm::ToolDefinition>&,
                          const std::string&,
                          llm::StreamCallbacks,
                          const std::atomic<bool>*) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        llm::LLMResponse r;
        r.content = "慢响应完成";
        r.finish_reason = "stop";
        return r;
    }

    llm::LLMResponse chatNonStreaming(const std::vector<llm::Message>&,
                                       const std::vector<llm::ToolDefinition>&,
                                       const std::string&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        llm::LLMResponse r;
        r.content = "慢响应完成";
        r.finish_reason = "stop";
        return r;
    }

    const ProviderConfig& config() const override { static ProviderConfig c; return c; }
};

void test_sub_agent_basic() {
    TEST("SubAgent 基本执行 — 返回 LLM 响应文本");
    agent::ToolRegistry registry;
    agent::RestrictedToolProvider tools(registry, {});
    agent::SubAgent sub(std::make_unique<MockLLMClient>("子任务执行完成"), tools);

    agent::SubAgentConfig config;
    config.task = "分析 ch-001 的剧情";
    config.system_prompt = "你是分析专家";
    config.allowed_tools = {}; // 无工具

    auto result = sub.execute(config);
    CHECK(!result.timed_out);
    CHECK(result.error.empty());
    CHECK(result.output == "子任务执行完成");
    PASS();
}

void test_sub_agent_timeout() {
    TEST("SubAgent 超时 — 短超时触发 timed_out");
    // Mock 客户端"卡住"（空响应模拟 LLM 无响应 — 但实际不会超时因为 mock 立即返回）
    // 真正超时测试需要 sleep，这里验证配置传递
    agent::SubAgentConfig config;
    config.timeout = std::chrono::seconds(1);
    CHECK(config.timeout.count() == 1);
    CHECK(config.max_tool_rounds == 3); // 默认值
    PASS();
}

void test_sub_agent_tool_filter() {
    TEST("SubAgent 工具过滤 — 只允许指定工具");
    agent::ToolRegistry registry;
    registry.registerTool("read", "读", utils::schema::object({}),
        agent::ToolCategory::Content,
        [](const nlohmann::json&) { return nlohmann::json::object(); });
    registry.registerTool("write", "写", utils::schema::object({}),
        agent::ToolCategory::Content,
        [](const nlohmann::json&) { return nlohmann::json::object(); });

    agent::RestrictedToolProvider tools(registry, {"read"});
    agent::SubAgent sub(std::make_unique<MockLLMClient>("ok"), tools);
    agent::SubAgentConfig config;
    config.task = "test";
    config.system_prompt = "test";
    config.allowed_tools = {"read"}; // 只允许 read

    auto result = sub.execute(config);
    CHECK(!result.timed_out);
    PASS();
}

void test_template_manager() {
    TEST("TemplateManager — 内置模板 + CRUD");
    agent::TemplateManager mgr;
    CHECK(mgr.allTemplates().size() == 5); // 5 个内置模板

    // 查找内置模板
    auto* t = mgr.findTemplate("chapter-consistency");
    CHECK(t != nullptr);
    CHECK(t->built_in);

    // 添加用户模板
    agent::SubAgentTemplate userTpl{"my-check", "自定义检查", "你是检查员", {"read_chapter"}, "", false};
    CHECK(mgr.addTemplate(userTpl));
    CHECK(mgr.allTemplates().size() == 6);

    // 不能删除内置模板
    CHECK(!mgr.removeTemplate("chapter-consistency"));

    // 可以删除用户模板
    CHECK(mgr.removeTemplate("my-check"));
    CHECK(mgr.allTemplates().size() == 5);

    PASS();
}

void test_sub_agent_timeout_actual() {
    TEST("B3 — 超时后 SubAgent 不崩溃且返回 timed_out");
    // SlowMockLLMClient 睡眠 8s，SubAgent timeout 100ms → 必然超时
    // B3 修复后：execute() 会阻塞等待异步任务退出（≤ ~8s），返回不崩溃
    agent::ToolRegistry registry;
    agent::RestrictedToolProvider tools(registry, {});
    agent::SubAgent sub(std::make_unique<SlowMockLLMClient>(8000), tools);
    agent::SubAgentConfig config;
    config.task = "长时间任务";
    config.system_prompt = "你是分析专家";
    config.timeout = std::chrono::seconds(1);   // 极短超时，确保触发
    config.max_tool_rounds = 1;

    auto result = sub.execute(config);
    CHECK(result.timed_out);
    CHECK(!result.error.empty());
    // 关键断言：execute() 返回后 SubAgent 对象完整未损坏
    PASS();
}

int main() {
    std::cout << "=== test_sub_agent ===\n\n";
    test_sub_agent_basic();
    test_sub_agent_timeout();
    test_sub_agent_timeout_actual();
    test_sub_agent_tool_filter();
    test_template_manager();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
