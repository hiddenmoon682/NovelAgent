#pragma once

/// 存储后端抽象接口 — 为 Phase 4 的 sqlite-vec 做准备。
///
/// 当前实现: FileStorageBackend（磁盘 JSON + Markdown 文件）
/// Phase 4 新增: SqliteStorageBackend（sqlite-vec 向量检索）
///
/// 所有项目 I/O 通过此接口进行，替换存储后端不影响上层工具代码。

#include <nlohmann/json.hpp>
#include <string>

class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    // ── JSON 文件读写 ──
    virtual nlohmann::json loadJson(const std::string& filePath) = 0;
    virtual void saveJson(const std::string& filePath,
                           const nlohmann::json& data) = 0;

    // ── 章节 Markdown 读写 ──
    virtual std::string readChapter(const std::string& filePath) = 0;
    virtual void writeChapter(const std::string& filePath,
                               const std::string& content) = 0;

    // ── 目录操作 ──
    virtual void createProjectDir(const std::string& path,
                                   const std::string& title) = 0;
    virtual bool exists(const std::string& path) const = 0;
};
