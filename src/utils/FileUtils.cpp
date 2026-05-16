#include "FileUtils.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

namespace utils::file {

std::string readText(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

void writeText(const std::string& path, const std::string& content) {
    // Ensure parent directory exists — project subdirectories may not exist yet
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

bool exists(const std::string& path) {
    return fs::exists(path);
}

bool isDir(const std::string& path) {
    return fs::is_directory(path);
}

void createDir(const std::string& path) {
    fs::create_directory(path);
}

void createDirs(const std::string& path) {
    fs::create_directories(path);
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> result;
    for (const auto& entry : fs::directory_iterator(path)) {
        result.push_back(entry.path().filename().string());
    }
    return result;
}

void removeDir(const std::string& path) {
    fs::remove_all(path);
}

void removeFile(const std::string& path) {
    fs::remove(path);
}

std::string joinPath(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

std::string dirName(const std::string& path) {
    return fs::path(path).parent_path().string();
}

std::string baseName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string homeDir() {
#ifdef _WIN32
    // USERPROFILE is the standard Windows equivalent of $HOME
    // (e.g. C:\Users\kami)
    const char* home = std::getenv("USERPROFILE");
    return home ? home : ".";
#else
    const char* home = std::getenv("HOME");
    return home ? home : ".";
#endif
}

std::string configDir() {
    return joinPath(homeDir(), ".novelagent");
}

} // namespace utils::file
