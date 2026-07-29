// FileStorageBackend 实现 — 适配 ProjectIO 到 IStorageBackend。

#include "project/FileStorageBackend.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

FileStorageBackend::FileStorageBackend(std::string project_path)
    : project_path_(std::move(project_path))
{}

// WHY 提取统一解析辅助：Issue 23 曾统一 loadJson/saveJson 的相对路径语义，
// 但解析逻辑内联重复两处，导致 exists() 漏改（相对路径误按进程 CWD
// 解析，与类级契约不一致）；收敛为单一辅助后三个方法共享同一规则，
// 杜绝再次分叉。
std::string FileStorageBackend::resolvePath(const std::string& filePath) const
{
    // 相对路径以 project_path_ 为基准解析；已包含项目路径的直接使用。
    if (!filePath.empty() && filePath.find(project_path_) != 0) {
        return project_path_ + "/" + filePath;
    }
    return filePath;
}

nlohmann::json FileStorageBackend::loadJson(const std::string& filePath)
{
    auto j = ProjectIO::loadJsonFile(resolvePath(filePath));
    return j.value_or(nlohmann::json{});
}

void FileStorageBackend::saveJson(const std::string& filePath,
                                   const nlohmann::json& data)
{
    ProjectIO::saveJsonFile(resolvePath(filePath), data);
}

std::string FileStorageBackend::readChapter(const std::string& filePath)
{
    return ProjectIO::readChapter(project_path_, filePath);
}

void FileStorageBackend::writeChapter(const std::string& filePath,
                                       const std::string& content)
{
    ProjectIO::writeChapter(project_path_, filePath, content);
}

void FileStorageBackend::createProjectDir(const std::string& path,
                                           const std::string& title)
{
    ProjectIO::createProjectDir(path, title);
}

bool FileStorageBackend::exists(const std::string& path) const
{
    // WHY 经 resolvePath 解析：此前相对路径原样透传，按进程 CWD 判断，
    // 违背类级契约“所有 filePath 相对路径以 project_path_ 解析”，与
    // loadJson/saveJson 语义不一致（Issue 23 补漏）。
    return utils::file::exists(resolvePath(path));
}

std::string FileStorageBackend::agentDir() const
{
    return ProjectIO::agentDir(project_path_);
}

std::string FileStorageBackend::nowTimestamp() const
{
    return ProjectIO::nowTimestamp();
}
