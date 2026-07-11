/// GetLatestChapterTool 实现 — 获取最新章节信息。

#include "agent/tools/GetLatestChapterTool.h"
#include "project/Models.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

using json = nlohmann::json;

json GetLatestChapterTool::parameters() const {
    return utils::schema::object({}, {});
}

json GetLatestChapterTool::execute(const json& /*args*/) {
    const auto& chapters = project_->outline.chapters;
    if (chapters.empty()) {
        return {{"info", "暂无章节，请先调用 create_chapter 创建第一章"}};
    }

    // 找到 order 最大的章节（线性增长，最后创建的就是最新的）
    const Chapter* latest = &chapters.front();
    for (const auto& ch : chapters) {
        if (ch.order > latest->order) {
            latest = &ch;
        }
    }

    json result;
    result["id"] = latest->id;
    result["title"] = latest->title;
    result["order"] = latest->order;
    result["status"] = latest->status;
    if (!latest->synopsis.empty()) result["synopsis"] = latest->synopsis;

    spdlog::info("[get_latest_chapter] {} '{}' (order={})",
                 latest->id, latest->title, latest->order);
    return result;
}

REGISTER_TOOL(agent::GetLatestChapterTool, "get_latest_chapter", get_latest_chapter)

} // namespace agent
