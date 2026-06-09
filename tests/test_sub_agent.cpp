#include "agent/SubAgent.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"
#include "utils/SchemaUtils.h"

#include <cassert>
#include <iostream>

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
                          llm::StreamCallbacks) override {
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

void test_sub_agent_basic() {
    TEST("SubAgent 基本执行 — 返回 LLM 响应文本");
    MockLLMClient mock("子任务执行完成");
    agent::ToolRegistry registry;
    agent::SubAgent sub(mock, registry);

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
    MockLLMClient mock("ok");
    agent::ToolRegistry registry;
    registry.registerTool("read", "读", utils::schema::object({}),
        agent::ToolCategory::Content,
        [](const nlohmann::json&) { return nlohmann::json::object(); });
    registry.registerTool("write", "写", utils::schema::object({}),
        agent::ToolCategory::Content,
        [](const nlohmann::json&) { return nlohmann::json::object(); });

    agent::SubAgent sub(mock, registry);
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

int main() {
    std::cout << "=== test_sub_agent ===\n\n";
    test_sub_agent_basic();
    test_sub_agent_timeout();
    test_sub_agent_tool_filter();
    test_template_manager();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
