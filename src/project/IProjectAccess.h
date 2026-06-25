#pragma once

/// 项目数据访问接口 — 分离读写权限，实现依赖倒置。
///
/// 只读工具（Get*/List*）仅接收 IProjectReader&，无法修改数据。
/// 写入工具（Create*/Update*/Write*）接收 Project&（同时实现两个接口）。
///
/// Phase 3.5 多 Agent 场景中，SubAgent 只能获取 IProjectReader&，
/// 确保并行子 Agent 不会相互干扰数据。

#include <string>
#include <vector>

struct Chapter;
struct Character;
struct Setting;
struct WorldRule;
struct Outline;
struct Style;

/// 只读项目访问接口 — 所有查询类工具依赖此接口。
class IProjectReader {
public:
    virtual ~IProjectReader() = default;

    virtual const std::vector<Chapter>& chapters() const = 0;
    virtual const std::vector<Character>& characters() const = 0;
    virtual const std::vector<Setting>& settings() const = 0;
    virtual const std::vector<WorldRule>& worldRules() const = 0;
    virtual const Outline& outline() const = 0;
    virtual const Style& style() const = 0;

    virtual const std::string& projectPath() const = 0;
    virtual const std::string& projectTitle() const = 0;
    virtual const std::string& logline() const = 0;
    virtual const std::string& theme() const = 0;

    /// 读取章节文件内容
    virtual std::string readChapterFile(const std::string& filePath) const = 0;
};

/// 可写项目访问接口 — 创建/修改类工具额外依赖此接口。
class IProjectWriter {
public:
    virtual ~IProjectWriter() = default;

    virtual std::vector<Chapter>& mutableChapters() = 0;
    virtual std::vector<Character>& mutableCharacters() = 0;
    virtual std::vector<Setting>& mutableSettings() = 0;
    virtual std::vector<WorldRule>& mutableWorldRules() = 0;
    virtual Outline& mutableOutline() = 0;

    /// 写入章节文件
    virtual void writeChapterFile(const std::string& filePath,
                                   const std::string& content) = 0;
    /// 全量保存项目到磁盘
    virtual void saveProject() = 0;
};
