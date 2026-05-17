#pragma once

// ProjectIO — 项目文件的磁盘读写。
// 每个小说项目是一个目录，包含多个 JSON 文件和 Markdown 章节文件。
// 本模块负责所有序列化/反序列化和文件系统操作。

#include "Models.h"
#include <string>
#include <nlohmann/json.hpp>

namespace ProjectIO {

// ── 目录结构创建 ──

// 在指定路径创建完整的项目目录骨架（含默认 JSON 文件）
// 如果目录已存在，不会覆盖已有文件，只创建缺失的
void createProjectDir(const std::string& path, const std::string& title);

// ── 项目加载/保存 ──

// 从目录路径加载完整项目（所有 JSON 文件 + 章节元数据）
// path 字段会被设置为传入的路径
Project load(const std::string& path);

// 保存项目的所有 JSON 数据到磁盘
// 注意：不保存 path、不保存 outline/chapters 内的 chapters（大纲另存）
void save(const Project& project);

// ── 分文件操作 ──

// 加载单个 JSON 文件到 nlohmann::json
// 文件不存在返回 nullopt，解析失败返回 nullopt
std::optional<nlohmann::json> loadJsonFile(const std::string& path);

// 保存 nlohmann::json 到文件（自动创建父目录）
void saveJsonFile(const std::string& path, const nlohmann::json& data);

// ── 章节读写 ──

// 读取章节 Markdown 文件，返回全文内容
// chapter_file_path 是相对于项目目录的路径（如 "chapters/001-intro.md"）
std::string readChapter(const std::string& projectPath, const std::string& chapterFilePath);

// 写入章节 Markdown 文件，自动创建父目录
void writeChapter(const std::string& projectPath, const std::string& chapterFilePath, const std::string& content);

// ── 对话历史 ──

// 加载对话历史（.novelagent/conversation.json）
// 返回 JSON 数组，每个元素包含 role 和 content
nlohmann::json loadConversation(const std::string& projectPath);

// 追加一条消息到对话历史
void appendConversation(const std::string& projectPath, const std::string& role, const std::string& content);

// 保存完整对话历史（覆盖写入）
void saveConversation(const std::string& projectPath, const nlohmann::json& conversation);

// ── 辅助函数 ──

// 在项目目录下定位章节文件的完整路径
std::string chapterPath(const std::string& projectPath, const std::string& chapterFilePath);

// 获取 .novelagent 子目录路径
std::string agentDir(const std::string& projectPath);

// 生成 ISO 8601 时间戳
std::string nowTimestamp();

} // namespace ProjectIO
