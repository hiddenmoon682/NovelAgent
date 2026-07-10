#pragma once

// 文件系统存储后端 — 封装项目路径，适配 ProjectIO 静态函数。
//
// 职责：持有 project_path_，为 SessionPersistence/ContextManager 提供
//   agentDir()/nowTimestamp()/saveJson()/loadJson() 等会话持久化所需的
//   存储 operations（底层转发到 ProjectIO 静态函数）。
//
// 设计说明（D4 修复）：此前存在 IStorageBackend 虚抽象接口，但其注释错误地
// 把 sqlite-vec 归到本接口——sqlite-vec 替换的是 IVectorStore（向量存储），
// 而非项目文件 I/O。且 ProjectIO::save/load 主读写路径根本不经过本接口，
// 抽象未统一入口、未换来可测试性（测试均用真实 FileStorageBackend，无 Mock），
// 违背 YAGNI。故删除 IStorageBackend，本类作为唯一具体实现直接被依赖。
// sqlite-vec 的扩展点在 IVectorStore（见 retrieval/IVectorStore.h）。

#include <nlohmann/json_fwd.hpp>

#include <string>

// 基于 ProjectIO 的文件系统存储后端。
//
// 路径语义（Issue 23 修复后统一）：
// - 所有接受 filePath 参数的方法，相对路径自动以 project_path_ 为基准解析
// - readChapter/writeChapter: filePath 为相对路径（如 "chapters/001.md"）
// - loadJson/saveJson: filePath 可以是相对路径（如 "conversation.json"）
//   或已包含 project_path_ 的绝对路径（向后兼容已有调用方）
class FileStorageBackend {
public:
    // @param project_path 项目根目录路径
    explicit FileStorageBackend(std::string project_path);

    nlohmann::json loadJson(const std::string& filePath);
    void saveJson(const std::string& filePath,
                  const nlohmann::json& data);

    std::string readChapter(const std::string& filePath);
    void writeChapter(const std::string& filePath,
                      const std::string& content);

    void createProjectDir(const std::string& path,
                          const std::string& title);
    bool exists(const std::string& path) const;

    std::string agentDir() const;
    std::string nowTimestamp() const;

private:
    std::string project_path_;
};
