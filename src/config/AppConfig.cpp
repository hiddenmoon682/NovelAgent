#include "AppConfig.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>

using json = nlohmann::json;

namespace {

// 把（可能是相对的）路径绝对化。
// 【为什么必须绝对化】save() 会把配置回写到 source_path；若记住的是相对路径，写回位置就会随
// 启动时的工作目录漂移——即"在哪启动就把设置/密钥写到哪"。实测过：以仓库根为工作目录启动时
// 加载的是 `<repo>/config.json`，此后任何设置保存都写进仓库里那份文件，而不是全局配置。
// 【为什么走 std::filesystem::path】本工具链（MinGW libstdc++）对窄字符串按 UTF-8 ↔ 宽字符
// 转换，中文路径安全；而 std::ifstream 的窄构造按系统 ANSI 解释路径、中文路径会打不开
//（见 AGENTS.md 记录）。
std::string absolutePath(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path), ec);
    if (ec) return path;   // 绝对化失败保持原样：诊断用途不得改变主流程
    return abs.string();
}

} // namespace

AppConfig AppConfig::load() {
    // 配置来源必须**确定**：只认唯一默认位置 ~/.novelagent/config.json，既不依赖进程的当前工作目录，
    // 也不提供"另一份配置"的额外入口——多一个来源就多一个"配置到底从哪来"的答案，而来源不唯一正是本模块
    // 踩过的坑：旧实现是"当前目录有 config.json 就用它"，于是工作目录里任何一个同名文件都会**静默**顶掉
    // 用户配置——实测把一份旧的 config.json 放在启动目录（仓库根）会导致 last_project_path 丢失：
    // 启动后既不恢复最近项目、也不报错，表现为"没有过去的会话"。
    // （设计取舍：曾加过环境变量 NOVELAGENT_CONFIG 作为"显式覆盖"出口、以及"检测到工作目录下有
    //   config.json 就警告一句"的迁移提示，评审后均移除——项目尚未发布，不存在需要迁移的存量用户，
    //   两者都是假想需求；确需用另一份配置时手工替换该文件即可。）
    const std::string globalPath = utils::file::joinPath(utils::file::configDir(), kDefaultConfigFile);
    if (utils::file::exists(globalPath)) {
        return loadFromFile(globalPath);
    }

    // 配置文件不存在时返回空配置，由调用方继续尝试环境变量（API key）等其他来源。
    return {};
}

AppConfig AppConfig::loadFromFile(const std::string& path) {
    AppConfig config;
    try {
        std::string content = utils::file::readText(path);
        json j = json::parse(content);

        config.default_provider = utils::json::getOrDefault(j, "default_provider", std::string("deepseek"));
        config.last_project_path = utils::json::getOrDefault(j, "last_project_path", std::string{});
        // 键缺失或为 null 时取空列表（首次加载尚未写入该键的文件不报错）
        config.recent_projects = utils::json::getOrDefault(j, "recent_projects", std::vector<std::string>{});
        config.verbose           = utils::json::getOrDefault(j, "verbose", false);

        if (j.contains("providers") && j["providers"].is_object()) {
            for (auto& [name, pj] : j["providers"].items()) {
                config.providers[name] = pj.get<ProviderConfig>();
            }
        }

        // 嵌入专用服务（可选段）：缺省时保持默认空配置（回退对话 provider 行为）
        if (j.contains("embedding") && j["embedding"].is_object()) {
            config.embedding = j["embedding"].get<EmbeddingSettings>();
        }
    } catch (const std::exception& e) {
        // 配置损坏时不让程序崩溃，记录警告后继续使用空配置。
        spdlog::warn("Failed to load config from {}: {}", path, e.what());
    }
    config.source_path = absolutePath(path);   // 绝对化：save() 回写位置不得随工作目录漂移
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
    // 嵌入专用服务（可选段）：未配置时仍写出空段，方便用户在 GUI 外手工填写
    j["embedding"] = embedding;
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
