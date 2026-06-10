#pragma once

/// IStorageBackend 的文件系统实现 — 适配 ProjectIO 静态函数。
///
/// Phase 4 架构改进：所有通过 IStorageBackend 访问存储的模块
/// 可通过此适配器间接使用 ProjectIO，保持依赖方向正确。

#include "project/IStorageBackend.h"

#include <string>

/// 基于 ProjectIO 的文件系统存储后端。
class FileStorageBackend : public IStorageBackend {
public:
    /// @param project_path 项目根目录路径
    explicit FileStorageBackend(std::string project_path);

    nlohmann::json loadJson(const std::string& filePath) override;
    void saveJson(const std::string& filePath,
                  const nlohmann::json& data) override;

    std::string readChapter(const std::string& filePath) override;
    void writeChapter(const std::string& filePath,
                      const std::string& content) override;

    void createProjectDir(const std::string& path,
                          const std::string& title) override;
    bool exists(const std::string& path) const override;

    std::string agentDir() const override;
    std::string nowTimestamp() const override;

private:
    std::string project_path_;
};
