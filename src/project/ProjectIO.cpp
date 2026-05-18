// ProjectIO 实现，负责项目文件的磁盘读写。
#include "ProjectIO.h"

#include "utils/FileUtils.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
namespace fu = utils::file;

namespace {

constexpr int kCurrentFormatVersion = 2;
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

// 新建项目时直接写入当前格式版本，并为扩展字段预留空容器。
json defaultNovelJson(const std::string& title) {
    const std::string ts = ProjectIO::nowTimestamp();
    return {
        {"format_version", kCurrentFormatVersion},
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
        {"modified", ts},
        {"tags", json::array()},
        {"metadata", json::object()}
    };
}

json defaultOutlineJson() {
    return {
        {"premise", ""},
        {"plot_threads", json::array()},
        {"chapters", json::array()}
    };
}

// 轻量迁移入口：
// 1. 把旧版 Setting::attributes 合并进 metadata
// 2. 将旧 format_version 提升到当前版本
// 这里先做无损、低风险的兼容处理，复杂迁移可以后续继续挂在这里。
void migrateProject(Project& project) {
    for (auto& setting : project.settings) {
        project::model_detail::mergeStringMapIntoMetadata(setting.metadata, setting.attributes);
        if (setting.attributes.empty()) {
            setting.attributes = project::model_detail::stringMapFromJsonValues(setting.metadata);
        }
    }

    if (project.format_version < kCurrentFormatVersion) {
        project.format_version = kCurrentFormatVersion;
    }
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
    fu::createDirs(path);
    fu::createDir(fu::joinPath(path, kChaptersDir));
    fu::createDir(fu::joinPath(path, kAgentDir));

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

    const std::string novelAgentDir = ProjectIO::agentDir(path);
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

        const std::string content = fu::readText(path);
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

    const auto novelJson = loadJsonFile(fu::joinPath(path, kNovelJson));
    if (novelJson) {
        project = novelJson->get<Project>();
    }

    const auto outlineJson = loadJsonFile(fu::joinPath(path, kOutlineJson));
    if (outlineJson) {
        project.outline = outlineJson->get<Outline>();
    }

    const auto charsJson = loadJsonFile(fu::joinPath(path, kCharactersJson));
    if (charsJson && charsJson->is_array()) {
        project.characters = charsJson->get<std::vector<Character>>();
    }

    const auto settingsJson = loadJsonFile(fu::joinPath(path, kSettingsJson));
    if (settingsJson && settingsJson->is_array()) {
        project.settings = settingsJson->get<std::vector<Setting>>();
    }

    const auto styleJson = loadJsonFile(fu::joinPath(path, kStyleJson));
    if (styleJson) {
        project.style = styleJson->get<Style>();
    }

    // 统一在加载末尾做迁移，这样无论数据来自哪个文件版本，内存中的模型都是当前形态。
    migrateProject(project);
    project.path = path;

    spdlog::info("Loaded project '{}' from {}", project.title, path);
    return project;
}

void ProjectIO::save(const Project& project) {
    const std::string& p = project.path;
    if (p.empty()) {
        throw std::runtime_error("Cannot save: project.path is empty");
    }

    Project mutableCopy = project;
    // 保存时始终写出当前格式，避免新旧格式在磁盘上继续混杂。
    mutableCopy.format_version = kCurrentFormatVersion;
    mutableCopy.modified = nowTimestamp();
    migrateProject(mutableCopy);

    const json novelJson = mutableCopy;
    saveJsonFile(fu::joinPath(p, kNovelJson), novelJson);
    saveJsonFile(fu::joinPath(p, kOutlineJson), mutableCopy.outline);
    saveJsonFile(fu::joinPath(p, kCharactersJson), mutableCopy.characters);
    saveJsonFile(fu::joinPath(p, kSettingsJson), mutableCopy.settings);
    saveJsonFile(fu::joinPath(p, kStyleJson), mutableCopy.style);

    spdlog::info("Saved project '{}' to {}", project.title, p);
}

std::string ProjectIO::chapterPath(const std::string& projectPath, const std::string& chapterFilePath) {
    return fu::joinPath(projectPath, chapterFilePath);
}

std::string ProjectIO::readChapter(const std::string& projectPath, const std::string& chapterFilePath) {
    const std::string fullPath = chapterPath(projectPath, chapterFilePath);
    if (!fu::exists(fullPath)) {
        spdlog::warn("Chapter file not found: {}", fullPath);
        return {};
    }
    return fu::readText(fullPath);
}

void ProjectIO::writeChapter(
    const std::string& projectPath,
    const std::string& chapterFilePath,
    const std::string& content) {
    const std::string fullPath = chapterPath(projectPath, chapterFilePath);
    fu::writeText(fullPath, content);
}

std::string ProjectIO::agentDir(const std::string& projectPath) {
    return fu::joinPath(projectPath, kAgentDir);
}

json ProjectIO::loadConversation(const std::string& projectPath) {
    const std::string convPath = fu::joinPath(agentDir(projectPath), kConversationJson);
    const auto j = loadJsonFile(convPath);
    if (j && j->is_array()) {
        return *j;
    }
    return json::array();
}

void ProjectIO::appendConversation(
    const std::string& projectPath,
    const std::string& role,
    const std::string& content) {
    json conv = loadConversation(projectPath);
    conv.push_back({
        {"role", role},
        {"content", content}
    });
    saveConversation(projectPath, conv);
}

void ProjectIO::saveConversation(const std::string& projectPath, const json& conversation) {
    const std::string convPath = fu::joinPath(agentDir(projectPath), kConversationJson);
    saveJsonFile(convPath, conversation);
}
