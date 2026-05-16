#include "AppConfig.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdlib>

using json = nlohmann::json;

AppConfig AppConfig::load() {
    std::string configPath = utils::file::joinPath(utils::file::configDir(), kDefaultConfigFile);
    if (utils::file::exists(configPath)) {
        return loadFromFile(configPath);
    }
    // Return empty config; caller will use env vars for API keys
    return {};
}

AppConfig AppConfig::loadFromFile(const std::string& path) {
    AppConfig config;
    try {
        std::string content = utils::file::readText(path);
        json j = json::parse(content);

        config.default_provider = utils::json::getOrDefault(j, "default_provider", std::string("deepseek"));

        if (j.contains("providers") && j["providers"].is_object()) {
            for (auto& [name, pj] : j["providers"].items()) {
                config.providers[name] = pj.get<ProviderConfig>();
            }
        }
    } catch (const std::exception& e) {
        // Don't crash on bad config — warn and continue with empty config
        spdlog::warn("Failed to load config from {}: {}", path, e.what());
    }
    return config;
}

void AppConfig::save(const std::string& path) const {
    json j;
    j["default_provider"] = default_provider;
    j["providers"] = json::object();
    for (const auto& [name, provider] : providers) {
        j["providers"][name] = provider;
    }
    utils::file::createDirs(utils::file::dirName(path));
    utils::file::writeText(path, j.dump(2));
}

const ProviderConfig* AppConfig::getProvider(const std::string& name) const {
    auto it = providers.find(name);
    if (it != providers.end()) {
        return &it->second;
    }
    return nullptr;
}

const ProviderConfig* AppConfig::getDefaultProvider() const {
    return getProvider(default_provider);
}

void AppConfig::setApiKey(const std::string& provider, const std::string& key) {
    providers[provider].api_key = key;
}

void AppConfig::addProvider(const std::string& name, const ProviderConfig& config) {
    providers[name] = config;
}
