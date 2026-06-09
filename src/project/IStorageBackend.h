#pragma once

/// 存储后端抽象接口 — 为 Phase 4 的 sqlite-vec 做准备。
///
/// 所有项目 I/O 通过此接口进行，替换存储后端不影响上层工具代码。
///
/// Phase 4 扩展：新增 agentDir() / nowTimestamp() 支持会话持久化和摘要缓存。

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

    // ── Phase 4 扩展：Agent 数据目录 ──
    virtual std::string agentDir() const = 0;

    // ── Phase 4 扩展：时间戳 ──
    virtual std::string nowTimestamp() const = 0;
};
