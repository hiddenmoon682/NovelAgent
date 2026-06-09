#include "agent/tools/ChapterTools.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/SchemaUtils.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {

using json = nlohmann::json;

// ===========================================================================
// 辅助: 按 ID 查找章节
// ===========================================================================

namespace {

const Chapter* findChapter(const std::vector<Chapter>& chapters,
                            const std::string& chapter_id)
{
    auto it = std::find_if(chapters.begin(), chapters.end(),
        [&](const Chapter& ch) { return ch.id == chapter_id; });
    return (it != chapters.end()) ? &(*it) : nullptr;
}

} // namespace

// ===========================================================================
// ReadChapterTool
// ===========================================================================

json ReadChapterTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID，例如 ch-001")}
    }, {"chapter_id"});
}

json ReadChapterTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    const auto* ch = findChapter(project_->outline.chapters, chapter_id);
    if (!ch) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    std::string content = ProjectIO::readChapter(project_->path, ch->file_path);
    spdlog::info("[read_chapter] {} → {} 字", chapter_id, content.size());

    return {
        {"chapter_id", ch->id},
        {"title", ch->title},
        {"content", content}
    };
}

// ===========================================================================
// WriteChapterTool
// ===========================================================================

json WriteChapterTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content", utils::schema::stringProp("章节 Markdown 完整内容")}
    }, {"chapter_id", "content"});
}

json WriteChapterTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    std::string content = args.value("content", "");

    const auto* ch = findChapter(project_->outline.chapters, chapter_id);
    if (!ch) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    ProjectIO::writeChapter(project_->path, ch->file_path, content);
    spdlog::info("[write_chapter] {} ← {} 字", chapter_id, content.size());

    return {{"success", true}, {"chapter_id", chapter_id}};
}

// ===========================================================================
// AppendChapterTool
// ===========================================================================

json AppendChapterTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content", utils::schema::stringProp("要追加的 Markdown 内容")}
    }, {"chapter_id", "content"});
}

json AppendChapterTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    std::string append_content = args.value("content", "");

    const auto* ch = findChapter(project_->outline.chapters, chapter_id);
    if (!ch) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    // 读取现有内容 → 追加 → 写回
    std::string existing = ProjectIO::readChapter(project_->path, ch->file_path);
    if (!existing.empty() && existing.back() != '\n') {
        existing += '\n';
    }
    std::string combined = existing + append_content;
    if (combined.back() != '\n') {
        combined += '\n';
    }

    ProjectIO::writeChapter(project_->path, ch->file_path, combined);
    spdlog::info("[append_to_chapter] {} += {} 字 (总计 {} 字)",
                 chapter_id, append_content.size(), combined.size());

    return {{"success", true}, {"chapter_id", chapter_id}};
}

// ===========================================================================
// ListChaptersTool
// ===========================================================================

json ListChaptersTool::parameters() const {
    return utils::schema::object({});
}

json ListChaptersTool::execute(const json& /*args*/) {
    json chapters = json::array();
    for (const auto& ch : project_->outline.chapters) {
        chapters.push_back({
            {"id", ch.id},
            {"title", ch.title},
            {"order", ch.order},
            {"file_path", ch.file_path},
            {"synopsis", ch.synopsis}
        });
    }

    spdlog::info("[list_chapters] 共 {} 章", chapters.size());
    return {{"chapters", std::move(chapters)}};
}

// ===========================================================================
// CreateChapterTool
// ===========================================================================

json CreateChapterTool::parameters() const {
    return utils::schema::object({
        {"title", utils::schema::stringProp("章节标题，例如 '发现'")},
        {"synopsis", utils::schema::stringProp("1-2 句章节摘要（可选）")}
    }, {"title"});
}

json CreateChapterTool::execute(const json& args) {
    std::string title = args.value("title", "");
    std::string synopsis = args.value("synopsis", "");

    if (title.empty()) {
        return {{"error", "章节标题不能为空"}};
    }

    // 计算新章节编号
    int max_order = 0;
    int max_ch_num = 0;
    for (const auto& ch : project_->outline.chapters) {
        max_order = std::max(max_order, ch.order);
        // 尝试从 id 中提取数字
        if (ch.id.size() >= 3 && ch.id.substr(0, 3) == "ch-") {
            try {
                int num = std::stoi(ch.id.substr(3));
                max_ch_num = std::max(max_ch_num, num);
            } catch (...) {}
        }
    }

    Chapter new_ch;
    new_ch.id = "ch-" + std::to_string(max_ch_num + 1);
    // 补零到 3 位，保持与既有章节的命名风格一致
    if (max_ch_num + 1 < 10)      new_ch.id = "ch-00" + std::to_string(max_ch_num + 1);
    else if (max_ch_num + 1 < 100) new_ch.id = "ch-0" + std::to_string(max_ch_num + 1);
    new_ch.title = title;
    new_ch.order = max_order + 1;
    new_ch.synopsis = synopsis;

    // 生成文件路径（用章节 ID 而非标题，避免 Windows 窄字符 API 下 UTF-8 路径问题）
    new_ch.file_path = "chapters/" + new_ch.id + ".md";

    // 添加到 outline 并全量保存（保证 outline.json 与其他 JSON 文件一致）
    project_->outline.chapters.push_back(new_ch);
    ProjectIO::save(*project_);

    // 写入空的章节文件
    std::string init_content = "# " + title + "\n\n";
    ProjectIO::writeChapter(project_->path, new_ch.file_path, init_content);

    spdlog::info("[create_chapter] {} '{}' → {}", new_ch.id, title, new_ch.file_path);

    return {
        {"success", true},
        {"chapter", {
            {"id", new_ch.id},
            {"title", new_ch.title},
            {"order", new_ch.order},
            {"file_path", new_ch.file_path}
        }}
    };
}

} // namespace agent

REGISTER_TOOL(agent::ReadChapterTool, "read_chapter", read_chapter)
REGISTER_TOOL(agent::WriteChapterTool, "write_chapter", write_chapter)
REGISTER_TOOL(agent::AppendChapterTool, "append_to_chapter", append_to_chapter)
REGISTER_TOOL(agent::ListChaptersTool, "list_chapters", list_chapters)
REGISTER_TOOL(agent::CreateChapterTool, "create_chapter", create_chapter)
