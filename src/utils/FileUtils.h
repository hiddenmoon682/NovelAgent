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
//
// @param path 文件路径。
// @return 全文内容；如果文件打不开，则返回空字符串。
std::string readText(const std::string& path);

// 将文本内容原子写入文件（先写临时文件再 rename 替换）。
// 如果父目录不存在，会自动创建。
//
// @param path    目标文件路径。
// @param content 待写入的全部内容。
// @throws std::runtime_error 临时文件创建/写入失败或原子替换失败时。
void writeText(const std::string& path, const std::string& content);

// 判断路径是否存在。
//
// @param path 待判断的路径。
// @return 存在返回 true。
bool exists(const std::string& path);

// 判断路径是否为目录。
//
// @param path 待判断的路径。
// @return 是目录返回 true。
bool isDir(const std::string& path);

// 创建单层目录。
// 如果父目录不存在，则由底层 filesystem 决定是否报错。
//
// @param path 目标目录路径。
void createDir(const std::string& path);

// 递归创建多层目录。
//
// @param path 目标目录路径，缺失的中间层级一并创建。
void createDirs(const std::string& path);

// 列出目录下的直接子项名称，不包含完整路径。
//
// @param path 目录路径。
// @return 子项名称列表（不保证顺序）。
// @throws std::filesystem::filesystem_error 路径不存在或不是目录时。
std::vector<std::string> listDir(const std::string& path);

// 递归删除目录及其全部内容。
//
// @param path 目标目录路径。
void removeDir(const std::string& path);

// 删除单个文件。
//
// @param path 目标文件路径。
void removeFile(const std::string& path);

// 拼接两段路径。
// 底层委托给 std::filesystem::path 处理分隔符。
//
// @param a 前半段路径。
// @param b 后半段路径。
// @return 拼接后的路径字符串。
std::string joinPath(const std::string& a, const std::string& b);

// 获取路径的父目录部分。
//
// @param path 输入路径。
// @return 父目录路径；无父目录时返回空字符串。
std::string dirName(const std::string& path);

// 获取路径最后一段名称。
//
// @param path 输入路径。
// @return 最后一段文件/目录名。
std::string baseName(const std::string& path);

// 获取当前用户主目录。
// Windows 下优先读取 USERPROFILE，Unix 下读取 HOME。
//
// @return 主目录路径；环境变量缺失时回退为 "."。
std::string homeDir();

// 获取应用配置目录，当前约定为 ~/.novelagent。
//
// @return 配置目录路径（不保证已存在，调用方需自行创建）。
std::string configDir();

} // namespace utils::file
