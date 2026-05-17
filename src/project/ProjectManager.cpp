// ProjectManager 实现 — 项目生命周期管理

#include "ProjectManager.h"
#include "ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace fu = utils::file;
namespace su = utils::string;

// ══════════════════════════════════════════════
// 创建项目
// ══════════════════════════════════════════════

Project ProjectManager::create(const std::string& path, const std::string& title) {
    spdlog::info("Creating project '{}' at {}", title, path);

    // 创建完整目录结构（不会覆盖已存在的文件）
    ProjectIO::createProjectDir(path, title);

    // 加载并返回
    Project project = ProjectIO::load(path);
    return project;
}

// ══════════════════════════════════════════════
// 打开项目
// ══════════════════════════════════════════════

Project ProjectManager::open(const std::string& path) {
    if (!isValid(path)) {
        spdlog::error("Invalid project directory: {}", path);
        return {};
    }
    return ProjectIO::load(path);
}

// ══════════════════════════════════════════════
// 自动判断打开或创建
// ══════════════════════════════════════════════

Project ProjectManager::openOrCreate(const std::string& path) {
    // 用目录名作为默认标题
    std::string dirName = fu::baseName(path);
    return openOrCreate(path, dirName);
}

Project ProjectManager::openOrCreate(const std::string& path, const std::string& title) {
    if (isValid(path)) {
        spdlog::info("Opening existing project at {}", path);
        return ProjectIO::load(path);
    }
    return create(path, title);
}

// ══════════════════════════════════════════════
// 验证
// ══════════════════════════════════════════════

bool ProjectManager::isValid(const std::string& path) const {
    // 目录必须存在，且包含 novel.json
    return fu::exists(path) &&
           fu::isDir(path) &&
           fu::exists(fu::joinPath(path, "novel.json"));
}

// ══════════════════════════════════════════════
// 列出项目
// ══════════════════════════════════════════════

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

// ══════════════════════════════════════════════
// 目录名生成
// ══════════════════════════════════════════════

std::string ProjectManager::getDefaultProjectDir(const std::string& title) {
    std::string result = title;

    // 替换中文和特殊字符为连字符或字母
    // 保留：字母、数字、中文、连字符、下划线
    // 删除：空格 → 连字符，其他特殊字符 → 删除
    std::string filtered;
    for (size_t i = 0; i < result.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(result[i]);
        if (c == ' ') {
            filtered += '-';
        } else if (c == '-' || c == '_' || std::isalnum(c)) {
            filtered += c;
        } else if (c >= 0x80) {
            // 多字节 UTF-8 字符（包括中文），保留
            // 简单策略：检查是否是 UTF-8 前导字节或后续字节
            if ((c & 0xC0) == 0x80) {
                // 后续字节，保留
                filtered += c;
            } else if ((c & 0x80) == 0x80) {
                // 多字节前导字节，保留
                filtered += c;
            } else {
                // 其他，跳过
            }
        }
        // 其他 ASCII 特殊字符跳过
    }

    if (filtered.empty()) {
        filtered = "untitled-novel";
    }

    return su::trimmed(filtered);
}
