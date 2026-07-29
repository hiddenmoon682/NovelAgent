// AppConfig 单元测试。
// 重点验证 ProviderConfig 序列化的字段迁移兼容（D2 修复）：
//   - 旧字段名 context_window 能被 from_json 读取为 max_context_tokens
//   - 新字段名 max_context_tokens 正常读取
//   - 两个字段同时存在时，新字段名优先
//   - to_json 只写新字段名（保存时升级格式）
//   - 缺失字段使用默认值（supports_cache_control 等旧 config 没有）
//   - 完整 config.json 往返：旧格式加载 → 保存 → 重载，值一致且已升级

#include "config/AppConfig.h"
#include "utils/FileUtils.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { ++tests_run; std::cout << "  TEST " << (name) << " ... "; } while (0)
#define PASS() \
    do { ++tests_passed; std::cout << "PASSED\n"; } while (0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << (msg) << '\n'; return; } while (0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); } } while (0)

const std::string kTestDir = "__test_app_config_tmp";

void cleanup() {
    if (utils::file::exists(kTestDir)) {
        utils::file::removeDir(kTestDir);
    }
}

void test_legacy_context_window_loaded() {
    TEST("D2 — 旧字段 context_window 被读取为 max_context_tokens");
    // 旧格式 config：用 context_window，无 supports_cache_control
    nlohmann::json pj = {
        {"name", "deepseek"},
        {"api_key", "sk-xxx"},
        {"base_url", "https://api.deepseek.com"},
        {"model", "deepseek-chat"},
        {"context_window", 65536},
        {"temperature", 0.7},
        {"max_tokens", 4096},
    };
    ProviderConfig c = pj.get<ProviderConfig>();
    CHECK(c.max_context_tokens == 65536);   // 旧字段值生效，而非默认 131072
    CHECK(c.name == "deepseek");
    CHECK(c.supports_cache_control == false); // 旧 config 无此字段，用默认值
    PASS();
}

void test_new_field_name_preferred() {
    TEST("D2 — 新字段 max_context_tokens 正常读取");
    nlohmann::json pj = {
        {"name", "kimi"},
        {"max_context_tokens", 200000},
    };
    ProviderConfig c = pj.get<ProviderConfig>();
    CHECK(c.max_context_tokens == 200000);
    PASS();
}

void test_both_present_new_wins() {
    TEST("D2 — 新旧字段同时存在时新字段优先");
    nlohmann::json pj = {
        {"name", "x"},
        {"max_context_tokens", 100000},
        {"context_window", 999},  // 应被忽略
    };
    ProviderConfig c = pj.get<ProviderConfig>();
    CHECK(c.max_context_tokens == 100000);
    PASS();
}

void test_missing_uses_default() {
    TEST("D2 — 两个字段都缺失时用默认值");
    nlohmann::json pj = {
        {"name", "x"},
    };
    ProviderConfig c = pj.get<ProviderConfig>();
    CHECK(c.max_context_tokens == 131072);
    PASS();
}

void test_to_json_writes_only_new_name() {
    TEST("D2 — to_json 只写新字段名（保存时升级）");
    ProviderConfig c;
    c.name = "deepseek";
    c.max_context_tokens = 65536;
    nlohmann::json pj = c;
    CHECK(pj.contains("max_context_tokens"));
    CHECK(!pj.contains("context_window"));  // 旧字段不应再写出
    CHECK(pj["max_context_tokens"] == 65536);
    PASS();
}

void test_thinking_config_defaults() {
    TEST("Thinking — enable_thinking 默认 false, reasoning_effort 默认 high");
    ProviderConfig c;
    CHECK(c.enable_thinking == false);
    CHECK(c.reasoning_effort == "high");
    PASS();
}

void test_thinking_config_roundtrip() {
    TEST("Thinking — JSON 往返");
    ProviderConfig c;
    c.name = "deepseek";
    c.enable_thinking = true;
    c.reasoning_effort = "max";
    nlohmann::json j = c;
    CHECK(j["enable_thinking"] == true);
    CHECK(j["reasoning_effort"] == "max");

    ProviderConfig c2 = j.get<ProviderConfig>();
    CHECK(c2.enable_thinking == true);
    CHECK(c2.reasoning_effort == "max");
    PASS();
}

void test_thinking_config_missing() {
    TEST("Thinking — 旧 config 缺失字段使用默认值");
    nlohmann::json j = {
        {"name", "deepseek"},
        {"api_key", "sk-xxx"},
    };
    ProviderConfig c = j.get<ProviderConfig>();
    CHECK(c.enable_thinking == false);   // 缺失→默认 false
    CHECK(c.reasoning_effort == "high"); // 缺失→默认 high
    PASS();
}

void test_roundtrip_legacy_config_upgraded() {
    TEST("D2 — 旧 config.json 往返：加载→保存→重载，值一致且已升级");
    cleanup();
    utils::file::createDirs(kTestDir);
    const std::string path = utils::file::joinPath(kTestDir, "config.json");

    // 写一份旧格式 config.json
    nlohmann::json oldCfg = {
        {"default_provider", "deepseek"},
        {"providers", {
            {"deepseek", {
                {"name", "deepseek"},
                {"api_key", "sk-legacy"},
                {"base_url", "https://api.deepseek.com"},
                {"model", "deepseek-chat"},
                {"context_window", 65536},
                {"temperature", 0.5},
                {"max_tokens", 8192},
            }}
        }}
    };
    utils::file::writeText(path, oldCfg.dump(2));

    // 加载：旧字段应生效
    AppConfig loaded = AppConfig::loadFromFile(path);
    const ProviderConfig* p = loaded.getProvider("deepseek");
    CHECK(p != nullptr);
    CHECK(p->max_context_tokens == 65536);
    CHECK(p->max_tokens == 8192);
    CHECK(p->temperature == 0.5);

    // 保存：应升级为新字段名
    loaded.save(path);
    const std::string saved = utils::file::readText(path);
    nlohmann::json reSaved = nlohmann::json::parse(saved);
    CHECK(reSaved["providers"]["deepseek"].contains("max_context_tokens"));
    CHECK(!reSaved["providers"]["deepseek"].contains("context_window"));
    CHECK(reSaved["providers"]["deepseek"]["max_context_tokens"] == 65536);

    // 重载：升级后的格式仍能正确读取
    AppConfig reloaded = AppConfig::loadFromFile(path);
    const ProviderConfig* p2 = reloaded.getProvider("deepseek");
    CHECK(p2 != nullptr);
    CHECK(p2->max_context_tokens == 65536);

    cleanup();
    PASS();
}

void test_gui_fields_roundtrip() {
    TEST("GUI 字段 last_project_path / verbose 保存后可重载");
    cleanup();
    AppConfig cfg;
    cfg.default_provider = "deepseek";
    // 仅作序列化往返的字符串值，不触碰文件系统；用无盘符路径避免硬编码盘符
    cfg.last_project_path = "novels/my-book";
    cfg.verbose = true;
    std::string path = kTestDir + "/config.json";
    cfg.save(path);

    AppConfig loaded = AppConfig::loadFromFile(path);
    CHECK(loaded.last_project_path == "novels/my-book");
    CHECK(loaded.verbose == true);
    // 旧配置没有这两个字段时应取默认值
    utils::file::writeText(path, R"({"default_provider":"deepseek","providers":{}})");
    AppConfig legacy = AppConfig::loadFromFile(path);
    CHECK(legacy.last_project_path.empty());
    CHECK(legacy.verbose == false);
    cleanup();
    PASS();
}

void test_ensure_default_providers() {
    TEST("ensureDefaultProviders 补齐缺失模板且不覆盖已有配置");
    AppConfig cfg;
    ProviderConfig mine;
    mine.name = "deepseek";
    mine.api_key = "sk-real";
    mine.model = "deepseek-v4-flash";
    cfg.providers["deepseek"] = mine;

    cfg.ensureDefaultProviders();
    CHECK(cfg.providers.size() == 3);                       // deepseek + kimi + claude
    CHECK(cfg.providers["deepseek"].api_key == "sk-real");  // 已有的不被覆盖
    CHECK(cfg.providers["deepseek"].model == "deepseek-v4-flash");
    CHECK(cfg.providers["kimi"].base_url == "https://api.moonshot.cn/v1");
    CHECK(cfg.providers["claude"].base_url == "https://api.anthropic.com");
    CHECK(cfg.providers["kimi"].api_key.empty());           // 模板不带 key
    PASS();
}

int main() {
    std::cout << "=== test_app_config (字段迁移兼容) ===\n\n";

    test_legacy_context_window_loaded();
    test_new_field_name_preferred();
    test_both_present_new_wins();
    test_missing_uses_default();
    test_to_json_writes_only_new_name();
    test_thinking_config_defaults();
    test_thinking_config_roundtrip();
    test_thinking_config_missing();
    test_roundtrip_legacy_config_upgraded();
    test_gui_fields_roundtrip();
    test_ensure_default_providers();

    cleanup();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return tests_passed == tests_run ? 0 : 1;
}
