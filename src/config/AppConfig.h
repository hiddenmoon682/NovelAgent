#pragma once

// AppConfig 用于管理 LLM 提供商配置和 API Key。
// 配置会以 JSON 形式持久化到 ~/.novelagent/config.json。
//
// DeepSeek、Kimi、Claude 都兼容 OpenAI 风格接口，
// 因此三者共用同一套 ProviderConfig 结构。
// 实际差异主要在 base_url、model 和 max_context_tokens。
//
// 字段迁移说明（D2 修复）：
//   max_context_tokens 原名 context_window（commit 51b7616 重命名）。
//   旧 config.json 仍用 context_window，from_json 会回退读取该旧字段名，
//   使现有用户配置继续生效；to_json 只写新字段名，保存时自动升级。

#include <string>
#include <map>
#include <nlohmann/json.hpp>

struct ProviderConfig {
    std::string name;                          // 提供商名称（如 "deepseek" / "kimi" / "claude"）
    std::string api_key;                       // API 密钥
    std::string base_url;                      // 接口地址，例如 "https://api.deepseek.com"
    std::string model;                         // 模型名，例如 "deepseek-v4-flash"
    int max_context_tokens = 1000000;          // 单次请求发送的最大上下文 token 数（应用层预算上限，非模型窗口大小）
    double temperature = 0.7;                  // 采样温度，越高输出越随机
    int max_tokens = 393216;                   // 单次回复的最大生成 token 数
    bool supports_cache_control = false;       // 是否支持显式 cache_control 标记（Anthropic API 需开启）
    bool enable_thinking = false;              // 是否启用 DeepSeek thinking 模式（默认关闭，token 消耗大）
    std::string reasoning_effort = "high";     // 推理努力度: "high" / "max" / "medium" / "low"
};

// 手写序列化（替代 NLOHMANN_DEFINE_TYPE_INTRUSIVE）：
// from_json 兼容旧字段名 context_window → max_context_tokens 的迁移；
// to_json 只写新字段名，保存时统一升级格式。
inline void to_json(nlohmann::json& j, const ProviderConfig& c) {
    j = nlohmann::json{
        {"name", c.name},
        {"api_key", c.api_key},
        {"base_url", c.base_url},
        {"model", c.model},
        {"max_context_tokens", c.max_context_tokens},
        {"temperature", c.temperature},
        {"max_tokens", c.max_tokens},
        {"supports_cache_control", c.supports_cache_control},
        {"enable_thinking", c.enable_thinking},
        {"reasoning_effort", c.reasoning_effort},
    };
}

inline void from_json(const nlohmann::json& j, ProviderConfig& c) {
    c.name                   = j.value("name", std::string{});
    c.api_key                = j.value("api_key", std::string{});
    c.base_url               = j.value("base_url", std::string{});
    c.model                  = j.value("model", std::string{});
    c.temperature            = j.value("temperature", 0.7);
    c.max_tokens             = j.value("max_tokens", 393216);
    c.supports_cache_control = j.value("supports_cache_control", false);
    c.enable_thinking        = j.value("enable_thinking", false);
    c.reasoning_effort       = j.value("reasoning_effort", "high");

    // 上下文窗口字段：优先新名 max_context_tokens；
    // 旧 config.json 用 context_window，回退读取以保持向后兼容。
    if (j.contains("max_context_tokens") && !j["max_context_tokens"].is_null()) {
        c.max_context_tokens = j["max_context_tokens"].get<int>();
    } else if (j.contains("context_window") && !j["context_window"].is_null()) {
        c.max_context_tokens = j["context_window"].get<int>();
    } else {
        c.max_context_tokens = 1000000;
    }
}

struct AppConfig {
    std::string default_provider = "deepseek";
    std::map<std::string, ProviderConfig> providers;

    // ── GUI 持久化字段 ──
    std::string last_project_path;  // 上次打开的项目目录，启动时自动恢复
    bool verbose = false;           // 调试日志开关

    // 运行时记录本配置的加载来源文件，不参与序列化。
    // save() 无参版本回写到该路径，避免“从 A 加载却存到 B”。
    std::string source_path;

    // 从默认位置 ~/.novelagent/config.json 加载配置。
    static AppConfig load();

    // 从指定路径加载配置。
    static AppConfig loadFromFile(const std::string& path);

    // 保存到指定路径，必要时自动创建父目录。
    void save(const std::string& path) const;

    // 回写到加载来源（source_path）；从未落盘过则写 defaultPath()。
    void save() const;

    // 全局配置文件路径：~/.novelagent/config.json
    static std::string defaultPath();

    // 为 deepseek / kimi / claude 补齐默认模板（base_url + model），
    // 已存在的 provider 不做任何修改。首次启动 GUI 向导依赖此方法。
    void ensureDefaultProviders();

    // 如果找不到对应 provider，则返回 nullptr。
    const ProviderConfig* getProvider(const std::string& name) const;
    const ProviderConfig* getDefaultProvider() const;

    // 便捷方法：为某个 provider 设置 API Key，不存在时自动创建条目。
    void setApiKey(const std::string& provider, const std::string& key);
    void addProvider(const std::string& name, const ProviderConfig& config);

    static constexpr const char* kDefaultConfigFile = "config.json";
};
