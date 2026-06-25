#pragma once

// AppConfig 用于管理 LLM 提供商配置和 API Key。
// 配置会以 JSON 形式持久化到 ~/.novelagent/config.json。
//
// DeepSeek、Kimi、Claude 都兼容 OpenAI 风格接口，
// 因此三者共用同一套 ProviderConfig 结构。
// 实际差异主要在 base_url、model 和 max_context_tokens。

#include <string>
#include <map>
#include <nlohmann/json.hpp>

struct ProviderConfig {
    std::string name;
    std::string api_key;
    std::string base_url;       // 例如 "https://api.deepseek.com"
    std::string model;          // 例如 "deepseek-chat"
    int max_context_tokens = 131072; // 每次请求发送给 LLM 的最大上下文 token 数（应用层预算上限，非模型上下文窗口大小）
    double temperature = 0.7;
    int max_tokens = 4096;

    // 使用 nlohmann 宏自动生成 to_json/from_json。
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProviderConfig,
        name, api_key, base_url, model, max_context_tokens, temperature, max_tokens)
};

struct AppConfig {
    std::string default_provider = "deepseek";
    std::map<std::string, ProviderConfig> providers;

    // 从默认位置 ~/.novelagent/config.json 加载配置。
    static AppConfig load();

    // 从指定路径加载配置。
    static AppConfig loadFromFile(const std::string& path);

    // 保存到指定路径，必要时自动创建父目录。
    void save(const std::string& path) const;

    // 如果找不到对应 provider，则返回 nullptr。
    const ProviderConfig* getProvider(const std::string& name) const;
    const ProviderConfig* getDefaultProvider() const;

    // 便捷方法：为某个 provider 设置 API Key，不存在时自动创建条目。
    void setApiKey(const std::string& provider, const std::string& key);
    void addProvider(const std::string& name, const ProviderConfig& config);

    static constexpr const char* kDefaultConfigFile = "config.json";
};
