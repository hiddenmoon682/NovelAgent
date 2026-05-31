#include "AppConfig.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdlib>

using json = nlohmann::json;

AppConfig AppConfig::load() {
    // 优先从当前目录加载 config.json，不存在则回退到 ~/.novelagent/config.json
    std::string localPath = kDefaultConfigFile; // 当前目录
    if (utils::file::exists(localPath)) {
        return loadFromFile(localPath);
    }

    std::string globalPath = utils::file::joinPath(utils::file::configDir(), kDefaultConfigFile);
    if (utils::file::exists(globalPath)) {
        return loadFromFile(globalPath);
    }

    // 配置文件不存在时返回空配置，由调用方继续尝试环境变量。
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
        // 配置损坏时不让程序崩溃，记录警告后继续使用空配置。
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
