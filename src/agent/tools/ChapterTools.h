#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

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
/// 支持在创建时填充叙事字段（goal/conflict/hook 等），减少后续手动编辑。
/// 参数: title (string, required), synopsis/goal/conflict/outcome/... (string, optional)
/// 返回: { success, chapter: { id, title, order, file_path } }
class CreateChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit CreateChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "create_chapter"; }
    std::string description() const override {
        return "创建新章节：在 outline 中新增条目并创建对应的 Markdown 文件。"
               "可选填写叙事简报字段（目标、冲突、转折点、伏笔等）。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 更新章节的创作简报字段。
/// 通过 fields 白名单机制安全写入，不在白名单中的字段会被静默忽略。
/// 参数: chapter_id (string, required), fields (object, required)
/// 返回: { success, chapter: { id, title, updated_fields } }
class UpdateChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_chapter"; }
    std::string description() const override {
        return "更新指定章节的创作简报字段（标题、概要、目标、冲突、转折点、"
               "伏笔、POV角色、关键事件等）。通过 fields 对象批量更新，"
               "不在白名单中的字段会被忽略。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 删除指定章节，并级联清理引用（PlotThread/Volume/Character/CharacterDevelopment 中对该章节 ID 的引用）。
/// 参数: chapter_id (string, required)
/// 返回: { success, deleted_id, cascade: { ... } }
class DeleteChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit DeleteChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "delete_chapter"; }
    std::string description() const override {
        return "删除指定章节（含 .md 文件），自动清理大纲/角色/剧情线中对该章节的引用。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

/// 更新章节的场景列表（完整替换）。
/// 参数: chapter_id (string, required), scenes (array of scene objects, required)
/// 返回: { success, chapter_id, scene_count }
class UpdateChapterScenesTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateChapterScenesTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "update_chapter_scenes"; }
    std::string description() const override {
        return "完整替换指定章节的场景列表（每项含 goal/conflict/turning_point 等戏剧单元字段）。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Content; }
};

} // namespace agent
