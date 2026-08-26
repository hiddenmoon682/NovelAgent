#include "FileUtils.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <atomic>

namespace fs = std::filesystem;

namespace utils::file {

namespace {
// 进程内自增计数器，用于生成临时文件后缀，避免并发写入同名目标时临时文件互撞。
// 不使用时间戳/随机数（脚本环境约束），PID + 单调计数足以保证进程内唯一。
std::atomic<unsigned long long> g_tmp_seq{0};
}  // namespace

std::string readText(const std::string& path) {
    // 用 std::filesystem::path 构造流：Windows 下窄字符 ifstream/ofstream 按 ANSI 代码页
    // （GBK）解释路径，UTF-8 中文路径会打开失败（历史 bug：创建中文项目名时 "无法创建临时文件"）；
    // path 重载内部走 UTF-8→宽字符转换，可正确读写中文路径。
    const fs::path p(path);
    std::ifstream f(p);
    if (!f) return {};

    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

// 原子写入：先写入同目录下的临时文件，再 fs::rename 替换目标。
// 这样即使写入过程中崩溃（断电、进程被杀、磁盘满），目标文件要么是完整的旧内容、
// 要么是完整的新内容，绝不会出现半截损坏的中间态。
// C++17 规范保证 fs::rename 在目标已存在时替换之（POSIX rename 语义）；
// MSVC 在 Windows 上用 MoveFileEx(MOVEFILE_REPLACE_EXISTING) 实现，同样为原子替换。
void writeText(const std::string& path, const std::string& content) {
    // 先确保父目录存在，避免首次写入项目子目录时失败。
    fs::create_directories(fs::path(path).parent_path());

    // 生成同目录下的唯一临时文件名：<path>.tmp.<seq>
    // 仅需进程内唯一即可——原子写防的是"写到一半崩溃"导致的半截文件，
    // 跨进程并发写冲突属于文件锁（B7）的范畴，不由临时文件名解决。
    const auto seq = g_tmp_seq.fetch_add(1, std::memory_order_relaxed);
    const std::string tmp_path = path + ".tmp." + std::to_string(seq);

    // 1) 写入临时文件。若打开失败，抛异常让上层统一处理。
    // ofstream 用 fs::path 构造，避开 Windows ANSI 代码页对 UTF-8 中文路径的破坏。
    {
        const fs::path tmp(tmp_path);
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            throw std::runtime_error("无法创建临时文件: " + tmp_path);
        }
        f << content;
        f.flush();
        if (!f) {
            // 写入过程中出错（磁盘满等）：删除半截临时文件，避免残留。
            std::error_code ec;
            fs::remove(tmp_path, ec);
            throw std::runtime_error("写入临时文件失败: " + tmp_path);
        }
    }  // 此处 f 析构关闭文件句柄，确保数据落盘后再 rename

    // 2) 原子替换目标文件。
    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        // 极少数平台边界（如目标被占用）rename 失败：回退为删除目标再 rename。
        fs::remove(path, ec);
        fs::rename(tmp_path, path, ec);
        if (ec) {
            std::error_code cleanup_ec;
            fs::remove(tmp_path, cleanup_ec);
            throw std::runtime_error("原子替换文件失败: " + tmp_path + " -> " + path +
                                     " (" + ec.message() + ")");
        }
    }
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
    // USERPROFILE 是 Windows 上与 $HOME 对应的标准环境变量。
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
