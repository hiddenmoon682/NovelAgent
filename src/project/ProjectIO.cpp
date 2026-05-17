// ProjectIO 实现，负责项目文件的磁盘读写。

#include "ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdexcept>

using json = nlohmann::json;
namespace fu = utils::file;

namespace {

// 项目目录下的固定文件和子目录名称。
constexpr const char* kNovelJson = "novel.json";
constexpr const char* kOutlineJson = "outline.json";
constexpr const char* kCharactersJson = "characters.json";
constexpr const char* kSettingsJson = "settings.json";
constexpr const char* kStyleJson = "style.json";
constexpr const char* kChaptersDir = "chapters";
constexpr const char* kAgentDir = ".novelagent";
constexpr const char* kConversationJson = "conversation.json";
constexpr const char* kSummariesJson = "summaries.json";
constexpr const char* kStateJson = "state.json";

// 生成默认的 novel.json 模板。
json defaultNovelJson(const std::string& title) {
    std::string ts = ProjectIO::nowTimestamp();
    return {
        {"format_version", 1},
        {"title", title},
        {"author", ""},
        {"description", ""},
        {"genre", json::array()},
        {"target_word_count", 0},
        {"current_word_count", 0},
        {"status", "planning"},
        {"pov", "third_person_limited"},
        {"tense", "past"},
        {"created", ts},
        {"modified", ts}
    };
}

// 生成默认的 outline.json 模板。
json defaultOutlineJson() {
    return {
        {"premise", ""},
        {"plot_threads", json::array()},
        {"chapters", json::array()}
    };
}

} // namespace

std::string ProjectIO::nowTimestamp() {
    auto now = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

void ProjectIO::createProjectDir(const std::string& path, const std::string& title) {
    // 先创建项目根目录和固定子目录。
    fu::createDirs(path);
    fu::createDir(fu::joinPath(path, kChaptersDir));
    fu::createDir(fu::joinPath(path, kAgentDir));

    // 只补齐缺失文件，避免覆盖用户已有内容。
    auto writeIfMissing = [](const std::string& filePath, const json& data) {
        if (!fu::exists(filePath)) {
            fu::writeText(filePath, data.dump(2) + "\n");
        }
    };

    writeIfMissing(fu::joinPath(path, kNovelJson), defaultNovelJson(title));
    writeIfMissing(fu::joinPath(path, kOutlineJson), defaultOutlineJson());
    writeIfMissing(fu::joinPath(path, kCharactersJson), json::array());
    writeIfMissing(fu::joinPath(path, kSettingsJson), json::array());
    writeIfMissing(fu::joinPath(path, kStyleJson), Style{});

    std::string novelAgentDir = ProjectIO::agentDir(path);
    writeIfMissing(fu::joinPath(novelAgentDir, kConversationJson), json::array());
    writeIfMissing(fu::joinPath(novelAgentDir, kSummariesJson), json::object());
    writeIfMissing(fu::joinPath(novelAgentDir, kStateJson), json::object());

    spdlog::info("Created project directory: {}", path);
}

std::optional<json> ProjectIO::loadJsonFile(const std::string& path) {
    try {
        if (!fu::exists(path)) {
            return std::nullopt;
        }

        std::string content = fu::readText(path);
        if (content.empty()) {
            return std::nullopt;
        }

        return json::parse(content);
    } catch (const json::parse_error& e) {
        spdlog::warn("JSON parse error in {}: {}", path, e.what());
        return std::nullopt;
    }
}

void ProjectIO::saveJsonFile(const std::string& path, const json& data) {
    fu::createDirs(fu::dirName(path));
    fu::writeText(path, data.dump(2) + "\n");
}

Project ProjectIO::load(const std::string& path) {
    Project project;

    // 先从 novel.json 加载顶层元数据。
    auto novelJson = loadJsonFile(fu::joinPath(path, kNovelJson));
    if (novelJson) {
        project = novelJson->get<Project>();
    }

    // 再分别装配 outline、角色、设定和风格。
    auto outlineJson = loadJsonFile(fu::joinPath(path, kOutlineJson));
    if (outlineJson) {
        project.outline = outlineJson->get<Outline>();
    }

    auto charsJson = loadJsonFile(fu::joinPath(path, kCharactersJson));
    if (charsJson && charsJson->is_array()) {
        project.characters = charsJson->get<std::vector<Character>>();
    }

    auto settingsJson = loadJsonFile(fu::joinPath(path, kSettingsJson));
    if (settingsJson && settingsJson->is_array()) {
        project.settings = settingsJson->get<std::vector<Setting>>();
    }

    auto styleJson = loadJsonFile(fu::joinPath(path, kStyleJson));
    if (styleJson) {
        project.style = styleJson->get<Style>();
    }

    // path 仅在运行时使用，这里在加载后补回去。
    project.path = path;

    spdlog::info("Loaded project '{}' from {}", project.title, path);
    return project;
}

void ProjectIO::save(const Project& project) {
    const std::string& p = project.path;
    if (p.empty()) {
        throw std::runtime_error("Cannot save: project.path is empty");
    }

    // 保存前刷新 modified 时间，避免写回过期时间戳。
    Project mutableCopy = project;
    mutableCopy.modified = nowTimestamp();

    json novelJson = mutableCopy;
    saveJsonFile(fu::joinPath(p, kNovelJson), novelJson);
    saveJsonFile(fu::joinPath(p, kOutlineJson), project.outline);
    saveJsonFile(fu::joinPath(p, kCharactersJson), project.characters);
    saveJsonFile(fu::joinPath(p, kSettingsJson), project.settings);
    saveJsonFile(fu::joinPath(p, kStyleJson), project.style);

    spdlog::info("Saved project '{}' to {}", project.title, p);
}

std::string ProjectIO::chapterPath(const std::string& projectPath, const std::string& chapterFilePath) {
    return fu::joinPath(projectPath, chapterFilePath);
}

std::string ProjectIO::readChapter(const std::string& projectPath, const std::string& chapterFilePath) {
    std::string fullPath = chapterPath(projectPath, chapterFilePath);
    if (!fu::exists(fullPath)) {
        spdlog::warn("Chapter file not found: {}", fullPath);
        return {};
    }
    return fu::readText(fullPath);
}

void ProjectIO::writeChapter(const std::string& projectPath, const std::string& chapterFilePath, const std::string& content) {
    std::string fullPath = chapterPath(projectPath, chapterFilePath);
    fu::writeText(fullPath, content);
}

std::string ProjectIO::agentDir(const std::string& projectPath) {
    return fu::joinPath(projectPath, kAgentDir);
}

json ProjectIO::loadConversation(const std::string& projectPath) {
    std::string convPath = fu::joinPath(agentDir(projectPath), kConversationJson);
    auto j = loadJsonFile(convPath);
    if (j && j->is_array()) {
        return *j;
    }
    return json::array();
}

void ProjectIO::appendConversation(const std::string& projectPath, const std::string& role, const std::string& content) {
    json conv = loadConversation(projectPath);

    json msg;
    msg["role"] = role;
    msg["content"] = content;
    conv.push_back(msg);

    saveConversation(projectPath, conv);
}

void ProjectIO::saveConversation(const std::string& projectPath, const json& conversation) {
    std::string convPath = fu::joinPath(agentDir(projectPath), kConversationJson);
    saveJsonFile(convPath, conversation);
}
