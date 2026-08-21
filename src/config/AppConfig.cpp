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
        config.last_project_path = utils::json::getOrDefault(j, "last_project_path", std::string{});
        // 旧配置可能没有 recent_projects 键：缺失时保持空列表，向后兼容。
        if (j.contains("recent_projects") && j["recent_projects"].is_array()) {
            // 逐元素过滤：数组中出现非字符串元素时跳过，避免整份配置解析失败
            config.recent_projects.clear();
            for (const auto& item : j["recent_projects"]) {
                if (item.is_string()) {
                    const std::string s = item.get<std::string>();
                    if (!s.empty()) {
                        config.recent_projects.push_back(s);
                    }
                }
            }
        }
        config.verbose           = utils::json::getOrDefault(j, "verbose", false);

        if (j.contains("providers") && j["providers"].is_object()) {
            for (auto& [name, pj] : j["providers"].items()) {
                config.providers[name] = pj.get<ProviderConfig>();
            }
        }
    } catch (const std::exception& e) {
        // 配置损坏时不让程序崩溃，记录警告后继续使用空配置。
        spdlog::warn("Failed to load config from {}: {}", path, e.what());
    }
    config.source_path = path;
    return config;
}

void AppConfig::save(const std::string& path) const {
    json j;
    j["default_provider"] = default_provider;
    j["last_project_path"] = last_project_path;
    j["recent_projects"] = recent_projects;
    j["verbose"] = verbose;
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

std::string AppConfig::defaultPath() {
    return utils::file::joinPath(utils::file::configDir(), kDefaultConfigFile);
}

void AppConfig::save() const {
    save(source_path.empty() ? defaultPath() : source_path);
}

void AppConfig::ensureDefaultProviders() {
    auto ensure = [this](const std::string& name, const std::string& url,
                         const std::string& model) {
        if (providers.count(name)) return;
        ProviderConfig p;
        p.name = name;
        p.base_url = url;
        p.model = model;
        providers[name] = p;
    };
    ensure("deepseek", "https://api.deepseek.com", "deepseek-v4-flash");
    ensure("kimi", "https://api.moonshot.cn/v1", "kimi-k2-turbo-preview");
    ensure("claude", "https://api.anthropic.com", "claude-sonnet-4-20250514");
}

// 记录一次项目打开：去重后置顶；空路径忽略。
void AppConfig::recordRecentProject(const std::string& path) {
    if (path.empty()) return;
    auto it = std::find(recent_projects.begin(), recent_projects.end(), path);
    if (it != recent_projects.end()) {
        recent_projects.erase(it);
    }
    recent_projects.insert(recent_projects.begin(), path);
}

// 从最近列表中移除项目目录；命中返回 true。
bool AppConfig::removeRecentProject(const std::string& path) {
    if (path.empty()) return false;
    auto it = std::find(recent_projects.begin(), recent_projects.end(), path);
    if (it == recent_projects.end()) return false;
    recent_projects.erase(it);
    return true;
}
