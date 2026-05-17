#pragma once

// ProjectManager — 管理小说项目的生命周期（创建、打开、验证、列表）。
// 委托 ProjectIO 执行实际的文件读写操作。

#include "Models.h"
#include <string>
#include <vector>

class ProjectManager {
public:
    // 创建新项目，返回已初始化的 Project
    Project create(const std::string& path, const std::string& title);

    // 打开已有项目，返回加载的 Project
    // 如果目录无效返回空 Project（title 为空）
    Project open(const std::string& path);

    // 自动判断：目录存在且有效 → 打开，否则 → 创建
    // 新建时用目录名作为标题
    Project openOrCreate(const std::string& path);

    // 自动判断，允许指定创建时的标题
    Project openOrCreate(const std::string& path, const std::string& title);

    // 验证目录是否包含有效的 novel.json
    bool isValid(const std::string& path) const;

    // 列出 baseDir 下所有有效的小说项目
    std::vector<std::string> listProjects(const std::string& baseDir) const;

    // 根据标题生成安全的目录名（替换空格和特殊字符）
    static std::string getDefaultProjectDir(const std::string& title);
};
