#pragma once

// ProjectManager 负责小说项目的生命周期管理，
// 包括创建、打开、校验和列出项目。
// 具体的磁盘读写仍由 ProjectIO 执行。

#include "project/Models/Project.h"
#include <string>
#include <vector>

class ProjectManager {
public:
    // 创建新项目，并返回已初始化完成的 Project。
    Project create(const std::string& path, const std::string& title);

    // 创建新项目并写入简介（description 为空时等价于双参版本）。
    Project create(const std::string& path, const std::string& title,
                   const std::string& description);

    // 打开已有项目并返回加载结果。
    // 如果目录无效，则返回空 Project（title 为空）。
    Project open(const std::string& path);

    // 自动判断：若目录有效则打开，否则创建。
    // 新建时默认使用目录名作为标题。
    Project openOrCreate(const std::string& path);

    // 自动判断：若目录有效则打开，否则按指定标题创建。
    Project openOrCreate(const std::string& path, const std::string& title);

    // 判断目录中是否包含有效的 novel.json。
    bool isValid(const std::string& path) const;

    // 列出 baseDir 下的所有有效小说项目。
    std::vector<std::string> listProjects(const std::string& baseDir) const;

    // 判断目录是否为软删项目（目录名带"（已删除）"标记）。
    static bool isSoftDeleted(const std::string& path);

    // 根据标题生成相对安全的目录名，尽量保留可读性。
    static std::string getDefaultProjectDir(const std::string& title);
};
