#pragma once

// AppConfig manages LLM provider settings and API keys.
// Persisted as JSON in ~/.novelagent/config.json.
//
// All three providers (DeepSeek, Kimi, Claude) use OpenAI-compatible APIs,
// so the same ProviderConfig struct works for all of them.
// Only base_url, model name, and context_window differ.

#include <string>
#include <map>
#include <nlohmann/json.hpp>

struct ProviderConfig {
    std::string name;
    std::string api_key;
    std::string base_url;       // e.g. "https://api.deepseek.com"
    std::string model;          // e.g. "deepseek-chat"
    int context_window = 65536; // model's max context window in tokens
    double temperature = 0.7;
    int max_tokens = 4096;

    // nlohmann macro: auto-generates to_json/from_json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProviderConfig,
        name, api_key, base_url, model, context_window, temperature, max_tokens)
};

struct AppConfig {
    std::string default_provider = "deepseek";
    std::map<std::string, ProviderConfig> providers;

    // Load from default location: ~/.novelagent/config.json
    static AppConfig load();

    // Load from explicit path
    static AppConfig loadFromFile(const std::string& path);

    // Save to path (creates parent directories)
    void save(const std::string& path) const;

    // Returns nullptr if provider not found
    const ProviderConfig* getProvider(const std::string& name) const;
    const ProviderConfig* getDefaultProvider() const;

    // Convenience: set API key for a provider (creates entry if needed)
    void setApiKey(const std::string& provider, const std::string& key);
    void addProvider(const std::string& name, const ProviderConfig& config);

    static constexpr const char* kDefaultConfigFile = "config.json";
};
