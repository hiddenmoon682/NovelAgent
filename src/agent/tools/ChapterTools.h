#pragma once

#include "agent/tools/BuiltInTool.h"
#include "project/Models.h"

#include <nlohmann/json.hpp>

namespace agent {

/// 读取章节全文。
/// 参数: chapter_id (string) — 章节 ID
/// 返回: { chapter_id, title, content }
class ReadChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit ReadChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "read_chapter"; }
    std::string description() const override {
        return "读取指定章节的 Markdown 全文内容";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 覆写章节内容。
/// 参数: chapter_id (string), content (string)
/// 返回: { success, chapter_id }
class WriteChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit WriteChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "write_chapter"; }
    std::string description() const override {
        return "覆写指定章节的 Markdown 内容（会替换原有全部内容）";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 在末尾追加内容到章节。
/// 参数: chapter_id (string), content (string)
/// 返回: { success, chapter_id }
class AppendChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit AppendChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "append_to_chapter"; }
    std::string description() const override {
        return "在指定章节末尾追加 Markdown 内容，保留原有内容";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 列出所有章节。
/// 参数: 无
/// 返回: { chapters: [{ id, title, order, file_path, synopsis }] }
class ListChaptersTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit ListChaptersTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "list_chapters"; }
    std::string description() const override {
        return "列出当前项目所有章节的 ID、标题、顺序、文件路径和摘要";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 创建新章节。
/// 参数: title (string), synopsis? (string)
/// 返回: { success, chapter: { id, title, order, file_path } }
class CreateChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreateChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_chapter"; }
    std::string description() const override {
        return "创建新章节：在 outline 中新增条目并创建对应的 Markdown 文件";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

} // namespace agent
