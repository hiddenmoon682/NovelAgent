#pragma once

// ProjectIO 负责项目文件在磁盘上的读写。
// 每个小说项目对应一个目录，里面包含多个 JSON 文件和 Markdown 章节文件。
// 这个模块统一处理序列化、反序列化以及项目目录结构操作。

#include "Models.h"
#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace ProjectIO {

// 目录结构创建

// 在指定路径创建完整项目骨架，并补齐默认 JSON 文件。
// 如果目录已经存在，则只创建缺失部分，不覆盖现有文件。
void createProjectDir(const std::string& path, const std::string& title);

// 项目加载与保存

// 从项目目录加载完整项目，包括各类 JSON 数据和章节元数据。
// 返回值中的 path 字段会被设置为传入路径。
Project load(const std::string& path);

// 将项目中的 JSON 数据写回磁盘。
// 注意：不会保存运行期字段 path。
void save(const Project& project);

// 单文件 JSON 读写

// 加载单个 JSON 文件并解析为 nlohmann::json。
// 文件不存在、为空或解析失败时返回 nullopt。
std::optional<nlohmann::json> loadJsonFile(const std::string& path);

// 保存 nlohmann::json 到文件，必要时自动创建父目录。
void saveJsonFile(const std::string& path, const nlohmann::json& data);

// 章节读写

// 读取章节 Markdown 文件并返回全文内容。
// chapterFilePath 是相对于项目根目录的路径，例如 "chapters/001-intro.md"。
std::string readChapter(const std::string& projectPath, const std::string& chapterFilePath);

// 写入章节 Markdown 文件，必要时自动创建父目录。
void writeChapter(const std::string& projectPath, const std::string& chapterFilePath, const std::string& content);

// 对话历史

// 加载 .novelagent/conversation.json。
// 返回 JSON 数组，每个元素包含 role 和 content。
nlohmann::json loadConversation(const std::string& projectPath);

// 追加一条消息到对话历史。
void appendConversation(const std::string& projectPath, const std::string& role, const std::string& content);

// 覆盖保存完整对话历史。
void saveConversation(const std::string& projectPath, const nlohmann::json& conversation);

// 辅助函数

// 计算章节文件在项目目录下的完整路径。
std::string chapterPath(const std::string& projectPath, const std::string& chapterFilePath);

// 获取 .novelagent 子目录路径。
std::string agentDir(const std::string& projectPath);

// 生成 ISO 8601 UTC 时间戳。
std::string nowTimestamp();

} // namespace ProjectIO
