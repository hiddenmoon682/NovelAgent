// FileStorageBackend 实现 — 适配 ProjectIO 到 IStorageBackend。

#include "project/FileStorageBackend.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

FileStorageBackend::FileStorageBackend(std::string project_path)
    : project_path_(std::move(project_path))
{}

nlohmann::json FileStorageBackend::loadJson(const std::string& filePath)
{
    // Issue 23: 统一路径语义 — 相对路径以 project_path_ 为基准解析。
    // 若 filePath 已经包含 project_path_（绝对路径），直接使用。
    if (!filePath.empty() && filePath.find(project_path_) != 0) {
        auto j = ProjectIO::loadJsonFile(project_path_ + "/" + filePath);
        return j.value_or(nlohmann::json{});
    }
    auto j = ProjectIO::loadJsonFile(filePath);
    return j.value_or(nlohmann::json{});
}

void FileStorageBackend::saveJson(const std::string& filePath,
                                   const nlohmann::json& data)
{
    // Issue 23: 统一路径语义 — 相对路径以 project_path_ 为基准解析。
    if (!filePath.empty() && filePath.find(project_path_) != 0) {
        ProjectIO::saveJsonFile(project_path_ + "/" + filePath, data);
    } else {
        ProjectIO::saveJsonFile(filePath, data);
    }
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
    return utils::file::exists(path);  // 注：utils::file 已通过 ProjectIO.h 间接可用
}

std::string FileStorageBackend::agentDir() const
{
    return ProjectIO::agentDir(project_path_);
}

std::string FileStorageBackend::nowTimestamp() const
{
    return ProjectIO::nowTimestamp();
}
