/// ChapterContextTools 实现 — 章节级上下文获取工具。

#include "agent/tools/ChapterContextTools.h"
#include "agent/prompt/PromptSelector.h"
#include "project/Models.h"
#include "project/ProjectAccess.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

json GetChapterContextTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID，例如 ch-003")}
    }, {"chapter_id"});
}

json GetChapterContextTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    if (chapter_id.empty()) {
        return {{"error", "chapter_id 不能为空"}};
    }

    // selector 返回指向 project 内部对象的指针，必须在锁内完成查询与构造
    return project_->withReadLock([&](const Project& p) -> json {
        const auto* chapter = prompt::selector::findById(
            p.outline.chapters, chapter_id);
        if (!chapter) {
            return {{"error", "章节 '" + chapter_id + "' 不存在"}};
        }

        json result;
        result["task"] = "write_chapter";
        result["chapter_id"] = chapter->id;

        // 章节元数据
        result["chapter"] = prompt::selector::filterObject(
            *chapter, false, {"id", "title"});

        // 卷信息（如所属）
        if (!chapter->volume_id.empty()) {
            const auto* volume = prompt::selector::findById(
                p.outline.volumes, chapter->volume_id);
            if (volume) {
                result["volume"] = prompt::selector::filterObject(
                    *volume, false, {"id", "title"});
            }
        }

        // 关联剧情线
        auto plotThreads = prompt::selector::selectPlotThreads(
            p, *chapter, 10);
        json ptArray = json::array();
        for (const auto* pt : plotThreads) {
            ptArray.push_back(prompt::selector::filterObject(
                *pt, false, {"id", "name"}));
        }
        result["plot_threads"] = ptArray;

        spdlog::info("[get_chapter_context] chapter={}, plot_threads={}",
                     chapter_id, plotThreads.size());
        return result;
    });
}

} // namespace agent

REGISTER_TOOL(agent::GetChapterContextTool, "get_chapter_context", get_chapter_context)
