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

// ── 匿名命名空间：文件常量与内部辅助函数 ──
namespace {

// 当前数据格式版本。新建项目和加载迁移均以此为准。
constexpr int kCurrentFormatVersion = 4;

// 项目根目录下的 JSON 文件名称。
// 这些短名常量统一引用 ProjectIO.h 中导出的公共常量（单一来源），
// 避免文件名字面量在头/实现/外部模块间重复定义导致漂移。
constexpr const char* kNovelJson = ProjectIO::kNovelJsonFileName;            // 小说元数据（标题、格式版本等）
constexpr const char* kOutlineJson = ProjectIO::kOutlineJsonFileName;        // 大纲（含 PlotThread 和 Chapter）
constexpr const char* kCharactersJson = ProjectIO::kCharactersJsonFileName;  // 角色列表
constexpr const char* kSettingsJson = ProjectIO::kSettingsJsonFileName;      // 设定列表
constexpr const char* kWorldRulesJson = ProjectIO::kWorldRulesJsonFileName;  // 世界规则列表
constexpr const char* kStyleJson = ProjectIO::kStyleJsonFileName;            // 写作风格

// 项目子目录名称。
constexpr const char* kChaptersDir = ProjectIO::kChaptersDirName;            // 章节 Markdown 文件存放目录
constexpr const char* kAgentDir = ProjectIO::kAgentDirName;                  // Agent 内部数据目录（隐藏）

// .novelagent 子目录下的文件名称（Agent 内部数据，不参与项目设定 staleness 检测，故不导出）。
constexpr const char* kConversationJson = "conversation.json"; // Agent 对话历史
constexpr const char* kSummariesJson = "summaries.json";       // 摘要/缓存数据
constexpr const char* kStateJson = "state.json";               // Agent 运行状态

// 生成默认 novel.json 内容。
// 使用 Project struct 构造，确保字段与 Models.h 定义一致，
// 新建时即写入当前格式版本并预留 metadata 空容器。
json defaultNovelJson(const std::string& title) {
    Project project;
    const std::string ts = ProjectIO::nowTimestamp();
    project.format_version = kCurrentFormatVersion;
    project.title = title;
    project.created = ts;
    project.modified = ts;
    return project;
}

// 生成默认 outline.json 内容（空大纲）。
json defaultOutlineJson() {
    return Outline{};
}

// 轻量项目升级入口。
// v1→v2: 预留
// v2→v3: 预留
// v3→v4: 新增 Volume（卷纲），旧项目的 volumes 默认为空数组，无需显式迁移。
void migrateProject(Project& project) {
    if (project.format_version < kCurrentFormatVersion) {
        project.format_version = kCurrentFormatVersion;
    }
}

} // namespace

// ── 时间戳 ──

// 返回当前 UTC 时间的 ISO 8601 字符串（精确到秒）。
// Windows 使用 gmtime_s，Unix 使用 gmtime_r。
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

// ── 目录与文件结构创建 ──

// 在指定路径创建完整的项目目录树。
// - 先创建根目录、章节目录和 .novelagent 目录
// - 再写入缺失的默认 JSON 文件（已有文件不覆盖）
void ProjectIO::createProjectDir(const std::string& path, const std::string& title) {
    fu::createDirs(path);
    fu::createDir(fu::joinPath(path, kChaptersDir));
    fu::createDir(fu::joinPath(path, kAgentDir));

    // 仅当文件不存在时才写入，避免覆盖用户已编辑的内容。
    auto writeIfMissing = [](const std::string& filePath, const json& data) {
        if (!fu::exists(filePath)) {
            fu::writeText(filePath, data.dump(2) + "\n");
        }
    };

    writeIfMissing(fu::joinPath(path, kNovelJson), defaultNovelJson(title));
    writeIfMissing(fu::joinPath(path, kOutlineJson), defaultOutlineJson());
    writeIfMissing(fu::joinPath(path, kCharactersJson), json::array());
    writeIfMissing(fu::joinPath(path, kSettingsJson), json::array());
    writeIfMissing(fu::joinPath(path, kWorldRulesJson), json::array());
    writeIfMissing(fu::joinPath(path, kStyleJson), Style{});

    const std::string novelAgentDir = ProjectIO::agentDir(path);
    writeIfMissing(fu::joinPath(novelAgentDir, kConversationJson), json::array());
    writeIfMissing(fu::joinPath(novelAgentDir, kSummariesJson), json::object());
    writeIfMissing(fu::joinPath(novelAgentDir, kStateJson), json::object());

    spdlog::info("Created project directory: {}", path);
}

// ── 单文件 JSON 读写 ──

// 加载并解析单个 JSON 文件。
// - 文件不存在 → nullopt
// - 文件为空 → nullopt
// - JSON 语法错误 → 日志警告 + nullopt
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

// 将 JSON 数据以两空格缩进格式写入文件。
// 父目录不存在时自动创建。
void ProjectIO::saveJsonFile(const std::string& path, const json& data) {
    fu::createDirs(fu::dirName(path));
    fu::writeText(path, data.dump(2) + "\n");
}

// ── 项目加载与保存 ──

// 从项目目录加载完整 Project。
// 各个 JSON 文件独立解析，缺失文件对应字段保持默认空值。
// 加载末尾调用 migrateProject 统一升级到当前格式版本。
Project ProjectIO::load(const std::string& path) {
    Project project;

    // 顶层元数据
    const auto novelJson = loadJsonFile(fu::joinPath(path, kNovelJson));
    if (novelJson) {
        project = novelJson->get<Project>();
    }

    // 大纲（含 PlotThread 和 Chapter）
    const auto outlineJson = loadJsonFile(fu::joinPath(path, kOutlineJson));
    if (outlineJson) {
        project.outline = outlineJson->get<Outline>();
    }

    // 角色列表
    const auto charsJson = loadJsonFile(fu::joinPath(path, kCharactersJson));
    if (charsJson && charsJson->is_array()) {
        project.characters = charsJson->get<std::vector<Character>>();
    }

    // 设定列表
    const auto settingsJson = loadJsonFile(fu::joinPath(path, kSettingsJson));
    if (settingsJson && settingsJson->is_array()) {
        project.settings = settingsJson->get<std::vector<Setting>>();
    }

    // 世界规则列表
    const auto worldRulesJson = loadJsonFile(fu::joinPath(path, kWorldRulesJson));
    if (worldRulesJson && worldRulesJson->is_array()) {
        project.world_rules = worldRulesJson->get<std::vector<WorldRule>>();
    }

    // 写作风格
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

// 将 Project 写回磁盘。
// - 保存前刷新 modified 时间戳，确保格式版本为当前版本
// - 各 JSON 文件分别写入
// - 使用可变副本避免修改调用方持有的 Project 对象
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

    // Issue 5: 增量保存 — 仅写入脏标记置位的文件。
    // dirty_flags == DIRTY_ALL 时全量写入（首次保存/手动保存）。
    // 注意：novel.json 始终写入（含 modified 时间戳 + format_version），
    // 避免 projectSettingsMtime 等 staleness 检测失效。
    const json novelJson = mutableCopy;
    saveJsonFile(fu::joinPath(p, kNovelJson), novelJson);

    // Safety guard: 脏标记为空但有子实体数据 → 工具可能漏调 markDirty()。
    // 此时全量写入以防静默数据丢失，并记录告警以便排查。
    uint32_t flags = project.dirty_flags;
    if (flags == 0) {
        bool has_entities = !mutableCopy.characters.empty()
                         || !mutableCopy.settings.empty()
                         || !mutableCopy.world_rules.empty();
        bool has_outline = !mutableCopy.outline.chapters.empty()
                        || !mutableCopy.outline.volumes.empty()
                        || !mutableCopy.outline.plot_threads.empty();
        if (has_entities || has_outline) {
            spdlog::warn("[ProjectIO] 脏标记为空但有子实体——可能遗漏 markDirty()，已全量保存");
            flags = Project::DIRTY_ALL;
        }
    }

    if (flags & Project::DIRTY_OUTLINE)
        saveJsonFile(fu::joinPath(p, kOutlineJson), mutableCopy.outline);
    if (flags & Project::DIRTY_CHARACTERS)
        saveJsonFile(fu::joinPath(p, kCharactersJson), mutableCopy.characters);
    if (flags & Project::DIRTY_SETTINGS)
        saveJsonFile(fu::joinPath(p, kSettingsJson), mutableCopy.settings);
    if (flags & Project::DIRTY_WORLD_RULES)
        saveJsonFile(fu::joinPath(p, kWorldRulesJson), mutableCopy.world_rules);
    if (flags & Project::DIRTY_STYLE)
        saveJsonFile(fu::joinPath(p, kStyleJson), mutableCopy.style);

    // save 成功，清除脏标记
    // 注意：mutableCopy 是栈上副本，需要修改原始 project 的 dirty_flags
    const_cast<Project&>(project).markClean();

    spdlog::info("Saved project '{}' to {} (dirty=0x{:x})", project.title, p, project.dirty_flags);
}

// ── 章节 Markdown 读写 ──

// 拼接项目路径与章节相对路径，得到完整文件路径。
std::string ProjectIO::chapterPath(const std::string& projectPath, const std::string& chapterFilePath) {
    return fu::joinPath(projectPath, chapterFilePath);
}

// 读取章节 Markdown 文件。
// 文件不存在时输出警告并返回空字符串。
std::string ProjectIO::readChapter(const std::string& projectPath, const std::string& chapterFilePath) {
    const std::string fullPath = chapterPath(projectPath, chapterFilePath);
    if (!fu::exists(fullPath)) {
        spdlog::warn("Chapter file not found: {}", fullPath);
        return {};
    }
    return fu::readText(fullPath);
}

// 写入章节 Markdown 文件，底层自动创建父目录。
void ProjectIO::writeChapter(
    const std::string& projectPath,
    const std::string& chapterFilePath,
    const std::string& content) {
    const std::string fullPath = chapterPath(projectPath, chapterFilePath);
    fu::writeText(fullPath, content);
}

// ── Agent 数据目录 ──

// .novelagent 目录路径，用于存储对话历史、摘要缓存、Agent 状态等。
std::string ProjectIO::agentDir(const std::string& projectPath) {
    return fu::joinPath(projectPath, kAgentDir);
}

// ── 对话历史 ──

// 加载对话历史 JSON 数组。
// 文件不存在或不是数组时返回空数组。
json ProjectIO::loadConversation(const std::string& projectPath) {
    const std::string convPath = fu::joinPath(agentDir(projectPath), kConversationJson);
    const auto j = loadJsonFile(convPath);
    if (j && j->is_array()) {
        return *j;
    }
    return json::array();
}

// 向对话历史追加一条消息。
// 先加载现有对话 → 拼入新消息 → 写回磁盘。
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

// 覆盖保存完整对话历史到 .novelagent/conversation.json。
void ProjectIO::saveConversation(const std::string& projectPath, const json& conversation) {
    const std::string convPath = fu::joinPath(agentDir(projectPath), kConversationJson);
    saveJsonFile(convPath, conversation);
}
