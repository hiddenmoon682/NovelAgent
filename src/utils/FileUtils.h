#pragma once

// 对 std::filesystem 的轻量封装。
// 将文件 I/O 集中在这里有几个好处：
//   - 错误处理可以统一收口。
//   - 以后若要补 Windows 的 UTF-8 路径兼容，不必修改所有调用方。
//   - 测试时也更容易替换或模拟文件系统行为。

#include <string>
#include <vector>
#include <optional>

namespace utils::file {

// 读取文本文件内容。
// 如果文件打不开，则返回空字符串。
std::string readText(const std::string& path);

// 将文本内容写入文件。
// 如果父目录不存在，会自动创建。
void writeText(const std::string& path, const std::string& content);

// 判断路径是否存在。
bool exists(const std::string& path);

// 判断路径是否为目录。
bool isDir(const std::string& path);

// 创建单层目录。
// 如果父目录不存在，则由底层 filesystem 决定是否报错。
void createDir(const std::string& path);

// 递归创建多层目录。
void createDirs(const std::string& path);

// 列出目录下的直接子项名称，不包含完整路径。
std::vector<std::string> listDir(const std::string& path);

// 递归删除目录及其全部内容。
void removeDir(const std::string& path);

// 删除单个文件。
void removeFile(const std::string& path);

// 拼接两段路径。
// 底层委托给 std::filesystem::path 处理分隔符。
std::string joinPath(const std::string& a, const std::string& b);

// 获取路径的父目录部分。
std::string dirName(const std::string& path);

// 获取路径最后一段名称。
std::string baseName(const std::string& path);

// 获取当前用户主目录。
// Windows 下优先读取 USERPROFILE，Unix 下读取 HOME。
std::string homeDir();

// 获取应用配置目录，当前约定为 ~/.novelagent。
std::string configDir();

} // namespace utils::file
