#pragma once

// Thin wrappers over std::filesystem.
// Centralizing file I/O here means:
//   - Error handling lives in one place.
//   - We can add UTF-8 path handling for Windows without touching callers.
//   - Tests can mock the filesystem by swapping this implementation.

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

// Path manipulation — delegates to std::filesystem::path
std::string joinPath(const std::string& a, const std::string& b);
std::string dirName(const std::string& path);
std::string baseName(const std::string& path);

// homeDir() returns USERPROFILE on Windows, HOME on Unix.
// configDir() is ~/.novelagent (where config.json and logs live).
std::string homeDir();
std::string configDir();

} // namespace utils::file
