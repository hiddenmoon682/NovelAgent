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

std::string readText(const std::string& path);
void writeText(const std::string& path, const std::string& content);
bool exists(const std::string& path);
bool isDir(const std::string& path);
void createDir(const std::string& path);
void createDirs(const std::string& path);
std::vector<std::string> listDir(const std::string& path);
void removeDir(const std::string& path);
void removeFile(const std::string& path);

// 路径处理，底层委托给 std::filesystem::path。
std::string joinPath(const std::string& a, const std::string& b);
std::string dirName(const std::string& path);
std::string baseName(const std::string& path);

// homeDir() 在 Windows 返回 USERPROFILE，在 Unix 返回 HOME。
// configDir() 对应 ~/.novelagent，用于保存配置和日志。
std::string homeDir();
std::string configDir();

} // namespace utils::file
