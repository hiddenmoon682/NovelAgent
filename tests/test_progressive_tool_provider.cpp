// test_progressive_tool_provider — 验证 ProgressiveToolProvider 渐进式工具加载闭环。
//
// 覆盖点（对应 src/agent/tool/ProgressiveToolProvider.cpp）：
//   1. 初始状态：仅核心工具 + tool_search（首位），不含延迟工具定义
//   2. 精确搜索（select:名称）激活延迟工具
//   3. 关键词模糊搜索（名称/描述子串）激活延迟工具
//   4. 搜索无匹配：返回引导信息，不激活任何工具
//   5. 执行拦截：未激活延迟工具返回引导错误且不执行真实逻辑，激活后放行
//   6. reset()：清空动态激活集合，回到初始状态
//   7. deferredToolsStub()：存根含延迟工具名+brief，不含完整 JSON schema
//   8. 核心工具始终直接可执行，不受激活状态影响
//
// 使用轻量函数式桩工具构造"核心 + 延迟"的可控工具集，
// 通过调用计数器验证"是否真正执行"，避免依赖真实业务工具的副作用。

#include "agent/tool/ProgressiveToolProvider.h"
#include "agent/tool/ToolRegistry.h"
#include "utils/SchemaUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

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

// =========================================================================
// 测试夹具：注册 2 个核心工具（名单见 ProgressiveToolProvider.cpp kCoreTools）
// 和 3 个延迟工具，并用计数器记录桩工具的真实执行次数。
// =========================================================================

struct Fixture {
    agent::ToolRegistry registry;
    int core_calls = 0;      // read_chapter 真实执行次数
    int deferred_calls = 0;  // get_world_rule 真实执行次数

    Fixture() {
        // --- 核心工具（名称必须命中 kCoreTools 名单才会被 initCoreTools 加载）---
        registry.registerTool(
            "read_chapter", "读取指定章节的正文内容",
            utils::schema::object({{"id", utils::schema::stringProp("章节ID")}}, {"id"}),
            agent::ToolCategory::System,
            [this](const json&) -> json { ++core_calls; return {{"ok", "core"}}; });
        registry.registerTool(
            "list_chapters", "列出全部章节标题",
            utils::schema::object({}),
            agent::ToolCategory::System,
            [](const json&) -> json { return {{"ok", "list"}}; });

        // --- 延迟工具（不在 kCoreTools 名单，需 tool_search 激活）---
        registry.registerTool(
            "get_character", "获取角色信息与人物档案",
            utils::schema::object({{"name", utils::schema::stringProp("角色名")}}, {"name"}),
            agent::ToolCategory::Character,
            [](const json&) -> json { return {{"ok", "character"}}; },
            "查角色档案");
        registry.registerTool(
            "update_style", "更新小说文风设置",
            utils::schema::object({}),
            agent::ToolCategory::System,
            [](const json&) -> json { return {{"ok", "style"}}; },
            "改写文风");
        registry.registerTool(
            "get_world_rule", "查询世界观规则条目",
            utils::schema::object({}),
            agent::ToolCategory::System,
            [this](const json&) -> json { ++deferred_calls; return {{"ok", "world"}}; },
            "查世界观规则");
    }
};

// 辅助：判断定义列表中是否含指定工具名。
static bool hasDef(const std::vector<llm::ToolDefinition>& defs, const std::string& name) {
    return std::any_of(defs.begin(), defs.end(),
        [&name](const llm::ToolDefinition& d) { return d.name == name; });
}

// =========================================================================
// 测试 1: 初始状态 — 核心工具 + tool_search（首位），不含延迟工具
// =========================================================================

void test_initial_definitions() {
    /**
     * 验证初始 getDefinitions() 的内容与顺序。
     *
     * 断言：
     *   - tool_search 位于列表首位（便于 LLM 发现）
     *   - 含全部已注册的核心工具（read_chapter/list_chapters）
     *   - 不含任何延迟工具定义（API 硬约束的前提）
     *   - loadedCount = 核心 2 + tool_search 1 = 3
     */
    TEST("初始状态：核心工具 + tool_search 首位，不含延迟工具");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    auto defs = provider.getDefinitions();
    CHECK(defs.size() == 3);
    CHECK(defs[0].name == "tool_search");
    CHECK(hasDef(defs, "read_chapter"));
    CHECK(hasDef(defs, "list_chapters"));
    CHECK(!hasDef(defs, "get_character"));
    CHECK(!hasDef(defs, "update_style"));
    CHECK(!hasDef(defs, "get_world_rule"));

    CHECK(provider.loadedCount() == 3);
    // has() 视未激活的延迟工具为"不存在"（与 tools 数组行为一致）
    CHECK(provider.has("tool_search"));
    CHECK(provider.has("read_chapter"));
    CHECK(!provider.has("get_character"));

    PASS();
}

// =========================================================================
// 测试 2: 精确搜索激活 — select:工具名
// =========================================================================

void test_exact_search_activation() {
    /**
     * 验证 tool_search(query="select:名称") 精确加载路径。
     *
     * 断言：
     *   - 返回 loaded_tools，含目标工具的完整 schema（parameters）
     *   - 激活后该工具出现在下一次 getDefinitions() 中
     */
    TEST("精确搜索 select: 激活延迟工具");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    auto result = provider.execute("tool_search", {{"query", "select:get_character"}});
    CHECK(result.contains("loaded_tools"));
    CHECK(result["loaded_tools"].size() == 1);
    CHECK(result["loaded_tools"][0]["name"] == "get_character");
    // 返回的必须是完整 JSON Schema，供 LLM 学习参数格式
    CHECK(result["loaded_tools"][0]["parameters"]["type"] == "object");
    CHECK(result["loaded_tools"][0]["parameters"]["properties"].contains("name"));

    auto defs = provider.getDefinitions();
    CHECK(defs.size() == 4);
    CHECK(hasDef(defs, "get_character"));
    CHECK(provider.has("get_character"));

    PASS();
}

// =========================================================================
// 测试 3: 关键词模糊搜索激活
// =========================================================================

void test_keyword_search_activation() {
    /**
     * 验证关键词模式：非精确名（名称子串 / 描述子串）也能命中并激活。
     *
     * 断言：
     *   - "style"（名称子串）命中 update_style
     *   - "角色"（描述子串）命中 get_character
     *   - 两者均出现在后续 getDefinitions() 中
     */
    TEST("关键词模糊搜索激活延迟工具");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    // 名称子串匹配
    auto r1 = provider.execute("tool_search", {{"query", "style"}});
    CHECK(r1.contains("loaded_tools"));
    CHECK(r1["loaded_tools"].size() == 1);
    CHECK(r1["loaded_tools"][0]["name"] == "update_style");

    // 描述子串匹配（中文关键词）
    auto r2 = provider.execute("tool_search", {{"query", "角色"}});
    CHECK(r2.contains("loaded_tools"));
    CHECK(r2["loaded_tools"][0]["name"] == "get_character");

    auto defs = provider.getDefinitions();
    CHECK(hasDef(defs, "update_style"));
    CHECK(hasDef(defs, "get_character"));
    CHECK(defs.size() == 5);

    PASS();
}

// =========================================================================
// 测试 4: 搜索不存在的工具 — 引导提示且不激活
// =========================================================================

void test_search_not_found() {
    /**
     * 验证无匹配时的引导路径。
     *
     * 断言：
     *   - 返回 message（含"未找到"）+ available_deferred_tools 列表（含未激活工具名）
     *   - 不激活任何工具（getDefinitions/loadedCount 不变）
     *   - select: 指向不存在的工具同样走"未找到"分支
     *   - 空 query 返回参数错误
     */
    TEST("搜索无匹配：引导提示且不激活任何工具");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    auto result = provider.execute("tool_search", {{"query", "zzzz_no_such"}});
    CHECK(result.contains("message"));
    CHECK(result["message"].get<std::string>().find("未找到") != std::string::npos);
    CHECK(result.contains("available_deferred_tools"));
    // 未加载列表应含全部延迟工具，引导 LLM 换关键词重搜
    const auto& avail = result["available_deferred_tools"];
    CHECK(std::find(avail.begin(), avail.end(), json("get_world_rule")) != avail.end());
    CHECK(std::find(avail.begin(), avail.end(), json("get_character")) != avail.end());

    // select: 不存在的工具 → 同样不激活
    auto r2 = provider.execute("tool_search", {{"query", "select:no_such_tool"}});
    CHECK(r2.contains("message"));
    CHECK(!r2.contains("loaded_tools"));

    // 空 query → 参数错误
    auto r3 = provider.execute("tool_search", {{"query", ""}});
    CHECK(r3.contains("error"));

    // 状态未被污染
    CHECK(provider.loadedCount() == 3);
    CHECK(provider.getDefinitions().size() == 3);

    PASS();
}

// =========================================================================
// 测试 5: 执行拦截 — 未激活拒绝并引导，激活后放行
// =========================================================================

void test_execute_interception() {
    /**
     * 验证 execute() 的第三层拦截（中间件强制）。
     *
     * 断言：
     *   - 未激活的延迟工具：返回引导错误（含 tool_search 提示、retryable），
     *     且真实工具逻辑未被执行（计数器为 0）
     *   - 通过 tool_search 激活后：正常放行，计数器 +1
     *   - 注册表中根本不存在的工具：返回"不存在"错误
     */
    TEST("执行拦截：未激活拒绝 + 激活后放行");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    // 未激活 → 引导错误，真实逻辑不执行
    auto blocked = provider.execute("get_world_rule", {});
    CHECK(blocked.contains("error"));
    CHECK(blocked["error"].get<std::string>().find("尚未加载") != std::string::npos);
    CHECK(blocked.contains("suggestion"));
    CHECK(blocked["suggestion"].get<std::string>().find("tool_search") != std::string::npos);
    CHECK(blocked["retryable"] == true);
    CHECK(f.deferred_calls == 0);

    // 激活后 → 放行并真实执行
    provider.execute("tool_search", {{"query", "select:get_world_rule"}});
    auto ok = provider.execute("get_world_rule", {});
    CHECK(ok["ok"] == "world");
    CHECK(f.deferred_calls == 1);

    // 注册表中不存在的工具 → 直接报不存在（无 suggestion）
    auto missing = provider.execute("totally_missing", {});
    CHECK(missing.contains("error"));
    CHECK(missing["error"].get<std::string>().find("不存在") != std::string::npos);
    CHECK(!missing.contains("suggestion"));

    PASS();
}

// =========================================================================
// 测试 6: reset — 会话边界清空激活集合
// =========================================================================

void test_reset() {
    /**
     * 验证 reset() 将激活状态还原到初始集合。
     *
     * 断言：
     *   - 激活 2 个延迟工具后定义数为 5
     *   - reset 后回到 3（tool_search + 2 核心），延迟工具全部消失
     *   - reset 后未激活工具再次被拦截（状态真正清空而非仅定义过滤）
     */
    TEST("reset()：激活后重置回初始集合");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    provider.execute("tool_search", {{"query", "select:get_character"}});
    provider.execute("tool_search", {{"query", "select:get_world_rule"}});
    CHECK(provider.getDefinitions().size() == 5);

    provider.reset();

    auto defs = provider.getDefinitions();
    CHECK(defs.size() == 3);
    CHECK(defs[0].name == "tool_search");
    CHECK(!hasDef(defs, "get_character"));
    CHECK(!hasDef(defs, "get_world_rule"));
    CHECK(provider.loadedCount() == 3);

    // reset 后拦截恢复生效
    auto blocked = provider.execute("get_world_rule", {});
    CHECK(blocked.contains("error"));
    CHECK(f.deferred_calls == 0);

    PASS();
}

// =========================================================================
// 测试 7: deferredToolsStub — 含名称与 brief，不含完整 schema
// =========================================================================

void test_deferred_tools_stub() {
    /**
     * 验证注入 system prompt 的延迟工具存根内容。
     *
     * 断言：
     *   - 含全部延迟工具名及其 brief（极简描述）
     *   - 不含核心工具名（核心工具始终直接可用，不进存根）
     *   - 不含 JSON schema 片段（properties/"type"），存根只做"目录"
     *   - 激活工具后存根不变（静态输出保证 KV cache 稳定，见实现注释）
     */
    TEST("deferredToolsStub：含延迟工具名+brief，不含 schema");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    std::string stub = provider.deferredToolsStub();
    CHECK(stub.find("get_character") != std::string::npos);
    CHECK(stub.find("查角色档案") != std::string::npos);
    CHECK(stub.find("update_style") != std::string::npos);
    CHECK(stub.find("get_world_rule") != std::string::npos);
    CHECK(stub.find("查世界观规则") != std::string::npos);

    // 核心工具不应出现在存根中
    CHECK(stub.find("read_chapter") == std::string::npos);
    CHECK(stub.find("list_chapters") == std::string::npos);

    // 存根只列名称+brief，不暴露完整 JSON Schema
    CHECK(stub.find("properties") == std::string::npos);
    CHECK(stub.find("\"type\"") == std::string::npos);

    // 存根输出静态：激活不改变内容（system prompt 会话期稳定）
    provider.execute("tool_search", {{"query", "select:get_character"}});
    CHECK(provider.deferredToolsStub() == stub);

    PASS();
}

// =========================================================================
// 测试 8: 核心工具始终直接可执行
// =========================================================================

void test_core_tools_always_executable() {
    /**
     * 验证核心工具不受渐进式激活机制影响。
     *
     * 断言：
     *   - 构造后未做任何 tool_search，核心工具即可执行（计数器验证真实执行）
     *   - 激活/重置延迟工具的过程不影响核心工具的可执行性
     */
    TEST("核心工具始终直接可执行");

    Fixture f;
    agent::ProgressiveToolProvider provider(f.registry);

    auto r1 = provider.execute("read_chapter", {{"id", "ch1"}});
    CHECK(r1["ok"] == "core");
    CHECK(f.core_calls == 1);

    // 中途激活延迟工具再 reset，核心工具不受影响
    provider.execute("tool_search", {{"query", "select:get_character"}});
    provider.reset();

    auto r2 = provider.execute("read_chapter", {{"id", "ch2"}});
    CHECK(r2["ok"] == "core");
    CHECK(f.core_calls == 2);

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_progressive_tool_provider ===\n\n";

    test_initial_definitions();
    test_exact_search_activation();
    test_keyword_search_activation();
    test_search_not_found();
    test_execute_interception();
    test_reset();
    test_deferred_tools_stub();
    test_core_tools_always_executable();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
