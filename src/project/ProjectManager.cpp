// ProjectManager 实现，负责项目生命周期管理。

#include "ProjectManager.h"
#include "ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>

namespace fu = utils::file;
namespace su = utils::string;
namespace fs = std::filesystem;

// 将路径解析为绝对路径（处理 MSYS2 /tmp→Windows Temp 等转换）。
static std::string resolvePath(const std::string& raw) {
    try {
        return fs::absolute(fs::path(raw)).string();
    } catch (...) {
        return raw;  // 解析失败时保持原样
    }
}

Project ProjectManager::create(const std::string& path, const std::string& title) {
    std::string absolute = resolvePath(path);
    spdlog::info("创建项目 '{}' 于 {}", title, absolute);

    // 先创建目录结构，再立即回读为完整 Project 对象。
    ProjectIO::createProjectDir(path, title);
    Project project = ProjectIO::load(path);
    return project;
}

Project ProjectManager::open(const std::string& path) {
    if (!isValid(path)) {
        spdlog::error("无效项目目录: {}", path);
        return {};
    }
    return ProjectIO::load(path);
}

Project ProjectManager::openOrCreate(const std::string& path) {
    // 未显式给标题时，默认使用目录名。
    std::string dirName = fu::baseName(path);
    return openOrCreate(path, dirName);
}

Project ProjectManager::openOrCreate(const std::string& path, const std::string& title) {
    if (isValid(path)) {
        spdlog::info("打开已有项目: {}", resolvePath(path));
        return ProjectIO::load(path);
    }
    spdlog::info("项目不存在，将在 {} 创建新项目", resolvePath(path));
    return create(path, title);
}

bool ProjectManager::isValid(const std::string& path) const {
    // 有效项目至少要是目录，并且存在 novel.json。
    return fu::exists(path) &&
           fu::isDir(path) &&
           fu::exists(fu::joinPath(path, "novel.json"));
}

std::vector<std::string> ProjectManager::listProjects(const std::string& baseDir) const {
    std::vector<std::string> result;

    if (!fu::exists(baseDir) || !fu::isDir(baseDir)) {
        return result;
    }

    for (const auto& entry : fu::listDir(baseDir)) {
        std::string fullPath = fu::joinPath(baseDir, entry);
        if (isValid(fullPath)) {
            result.push_back(fullPath);
        }
    }

    return result;
}

std::string ProjectManager::getDefaultProjectDir(const std::string& title) {
    std::string result = title;

    // 尽量保留字母、数字、中文、连字符和下划线，
    // 空格转成连字符，其余不安全字符直接跳过。
    std::string filtered;
    for (size_t i = 0; i < result.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(result[i]);
        if (c == ' ') {
            filtered += '-';
        } else if (c == '-' || c == '_' || std::isalnum(c)) {
            filtered += c;
        } else if (c >= 0x80) {
            // 对 UTF-8 多字节内容不做深入解析，先按字节原样保留。
            filtered += c;
        }
        // 其余 ASCII 特殊字符直接忽略。
    }

    if (filtered.empty()) {
        filtered = "untitled-novel";
    }

    return su::trimmed(filtered);
}
