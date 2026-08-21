#pragma once

// ProjectIO 负责项目文件在磁盘上的读写。
// 每个小说项目对应一个目录，里面包含多个 JSON 文件和 Markdown 章节文件。
// 这个模块统一处理序列化、反序列化以及项目目录结构操作。
//
// 项目文件结构:
//   <project>/
//     novel.json          — 项目顶层元数据
//     outline.json        — 大纲（含 Volume、PlotThread 和 Chapter）
//     characters.json     — 角色列表（含 Relationship）
//     settings.json       — 设定列表
//     world_rules.json    — 世界规则列表
//     style.json          — 写作风格配置
//     chapters/           — 章节 Markdown 文件
//     .novelagent/
//       novel.db          — 会话/消息/索引清单/向量（SQLite，由 SessionPersistence 与索引服务管理）
//       summaries.json    — 章节摘要缓存
//       state.json        — Agent 运行时状态

#include "project/Models/Project.h"
#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace ProjectIO {

// ── 目录结构创建 ──

// 在指定路径创建完整项目骨架，包括子目录和默认 JSON 文件。
// 如果部分文件已存在则跳过，不会覆盖用户已有内容。
void createProjectDir(const std::string& path, const std::string& title);

// ── 项目加载与保存 ──

// 从目录加载完整 Project，包括所有 JSON 文件和章节元数据。
// 加载末尾自动调用 migrateProject() 升级旧格式到当前版本。
// 返回的 Project::path 会被设置为传入的项目路径。
Project load(const std::string& path);

// 将 Project 的 JSON 数据写回磁盘。
// 保存前会刷新 modified 时间戳并确保格式版本为最新。
// 注意：path 字段不会被写入 JSON。
//
// 副作用：保存成功后会调用 project.markClean() 清除脏标记，
// 因此参数为非 const 引用（D4：取代此前的 const_cast 写法）。
void save(Project& project);

// 序列化给定 Project 快照并按 flags 指定的脏位写盘（P3 并发落盘）。
// 与 save() 的区别：不读取/修改调用方共享状态的 dirty_flags、
// 不做 markClean——调用方（ProjectAccess::save）已在锁内完成
// 快照拷贝与脏标记 test-and-clear，本函数只做纯序列化 + 文件 IO。
void saveSnapshot(Project& data, uint32_t flags);

// ── 单文件 JSON 读写 ──

// 加载并解析单个 JSON 文件。
// 文件不存在、为空或 JSON 格式错误时返回 nullopt。
std::optional<nlohmann::json> loadJsonFile(const std::string& path);

// 轻量读取项目标题：仅解析 <path>/novel.json 的 title 字段，不加载整棵树
// （供最近项目列表等只读场景使用）；目录无效或无 title 时返回空串。
std::string peekTitle(const std::string& path);

// 将 nlohmann::json 写入文件，自动创建不存在的父目录。
// 输出格式为两空格缩进、末尾换行。
void saveJsonFile(const std::string& path, const nlohmann::json& data);

// ── 章节 Markdown 读写 ──

// 读取章节 Markdown 文件并返回全文内容。
// chapterFilePath 是相对于项目根目录的路径，例如 "chapters/001-intro.md"。
// 文件不存在时返回空字符串并输出警告日志。
std::string readChapter(const std::string& projectPath, const std::string& chapterFilePath);

// 写入章节 Markdown 文件，必要时自动创建父目录。
void writeChapter(const std::string& projectPath, const std::string& chapterFilePath, const std::string& content);

// ── 路径辅助函数 ──

// 项目根目录下各 JSON 文件的固定文件名。
// 导出为公共常量，供 ContextManager（staleness 检测）等外部模块复用，
// 避免文件名字面量散落各处导致漂移（曾出现 ContextManager 写死 "project.json"
// 而实际文件名为 "novel.json" 的静默失效 bug）。
constexpr const char* kNovelJsonFileName     = "novel.json";       // 小说元数据
constexpr const char* kOutlineJsonFileName   = "outline.json";     // 大纲
constexpr const char* kCharactersJsonFileName= "characters.json";  // 角色列表
constexpr const char* kSettingsJsonFileName  = "settings.json";    // 设定列表
constexpr const char* kWorldRulesJsonFileName= "world_rules.json"; // 世界规则列表
constexpr const char* kStyleJsonFileName     = "style.json";       // 写作风格
constexpr const char* kChaptersDirName       = "chapters";         // 章节目录
constexpr const char* kAgentDirName          = ".novelagent";      // Agent 内部数据目录

// 计算章节 Markdown 文件在项目目录下的绝对路径。
std::string chapterPath(const std::string& projectPath, const std::string& chapterFilePath);

// 返回项目内的 .novelagent 子目录路径。
std::string agentDir(const std::string& projectPath);

// 生成当前 UTC 时间的 ISO 8601 格式时间戳字符串。
std::string nowTimestamp();

} // namespace ProjectIO
