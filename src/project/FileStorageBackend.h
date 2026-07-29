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
// - loadJson/saveJson: filePath 可以是相对路径（如 "sessions/index.json"）
//   或已包含 project_path_ 的绝对路径（向后兼容已有调用方）
class FileStorageBackend {
public:
    // 构造后端，绑定项目根目录。
    //
    // @param project_path 项目根目录路径，按值保存；后续相对路径均以此为基准解析。
    explicit FileStorageBackend(std::string project_path);

    // 加载并解析 JSON 文件。
    //
    // @param filePath 相对项目根目录的路径，或已含项目路径的绝对路径。
    // @return 解析后的 JSON；文件不存在/为空/格式错误时返回 null JSON。
    nlohmann::json loadJson(const std::string& filePath);

    // 将 JSON 写入文件，自动创建不存在的父目录。
    //
    // @param filePath 相对项目根目录的路径，或已含项目路径的绝对路径。
    // @param data     待写入的 JSON 数据。
    void saveJson(const std::string& filePath,
                  const nlohmann::json& data);

    // 读取章节 Markdown 文件内容。
    //
    // @param filePath 相对项目根目录的路径（如 "chapters/001.md"）。
    // @return 全文内容；文件不存在时返回空字符串。
    std::string readChapter(const std::string& filePath);

    // 写入章节 Markdown 文件，必要时自动创建父目录。
    //
    // @param filePath 相对项目根目录的路径。
    // @param content  章节全文内容。
    void writeChapter(const std::string& filePath,
                      const std::string& content);

    // 在指定路径创建完整项目骨架（转发 ProjectIO::createProjectDir）。
    //
    // @param path  目标项目目录（注意：此参数独立于 project_path_，不做相对路径解析）。
    // @param title 项目标题。
    void createProjectDir(const std::string& path,
                          const std::string& title);

    // 判断路径是否存在。
    //
    // @param path 相对项目根目录的路径，或已含项目路径的绝对路径
    //             （与 loadJson/saveJson 相同的解析规则）。
    // @return 路径存在返回 true。
    bool exists(const std::string& path) const;

    // 返回项目内的 .novelagent 子目录路径。
    std::string agentDir() const;

    // 生成当前 UTC 时间的 ISO 8601 格式时间戳字符串。
    std::string nowTimestamp() const;

private:
    // 统一路径解析：相对路径以 project_path_ 为基准，已含项目路径的直接返回。
    std::string resolvePath(const std::string& filePath) const;

    std::string project_path_;
};
