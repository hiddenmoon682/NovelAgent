#include "agent/tools/OutlineTools.h"
#include "project/Models.h"
#include "utils/SchemaUtils.h"
#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

json GetOutlineTool::parameters() const {
    return utils::schema::object({});
}
json GetOutlineTool::execute(const json&) {
    const auto& o = project_->outline;
    json vol_arr = json::array();
    for (const auto& v : o.volumes)
        vol_arr.push_back({{"id", v.id}, {"title", v.title}, {"order", v.order}});
    json plot_arr = json::array();
    for (const auto& pt : o.plot_threads)
        plot_arr.push_back({{"id", pt.id}, {"name", pt.name}, {"type", pt.type}, {"status", pt.status}});
    json ch_arr = json::array();
    for (const auto& c : o.chapters)
        ch_arr.push_back({{"id", c.id}, {"title", c.title}, {"order", c.order}, {"synopsis", c.synopsis}});
    spdlog::info("[get_outline] {}卷 {}剧情线 {}章", vol_arr.size(), plot_arr.size(), ch_arr.size());
    return {
        {"premise", o.premise},
        {"story_structure", o.story_structure},
        {"volumes", vol_arr},
        {"plot_threads", plot_arr},
        {"chapters", ch_arr}
    };
}

json GetProjectStatusTool::parameters() const {
    return utils::schema::object({});
}
json GetProjectStatusTool::execute(const json&) {
    spdlog::info("[get_project_status] {}", project_->title);
    return {
        // ── 基本信息 ──
        {"title", project_->title},
        {"logline", project_->logline},
        {"theme", project_->theme},
        {"description", project_->description},
        {"genre", project_->genre},
        {"comps", project_->comps},
        {"central_question", project_->central_question},
        // ── 目标与约束 ──
        {"target_audience", project_->target_audience},
        {"content_rating", project_->content_rating},
        {"ending_type", project_->ending_type},
        {"must_have_elements", project_->must_have_elements},
        {"must_avoid_elements", project_->must_avoid_elements},
        {"narrative_promises", project_->narrative_promises},
        {"world_rules_summary", project_->world_rules_summary},
        // ── 进度 ──
        {"target_word_count", project_->target_word_count},
        {"current_word_count", project_->current_word_count},
        {"status", project_->status},
        // ── 统计 ──
        {"characters_count", static_cast<int>(project_->characters.size())},
        {"chapters_count", static_cast<int>(project_->outline.chapters.size())},
        {"settings_count", static_cast<int>(project_->settings.size())},
        {"world_rules_count", static_cast<int>(project_->world_rules.size())},
        // ── 元数据 ──
        {"created", project_->created},
        {"modified", project_->modified}
    };
}

} // namespace agent

REGISTER_TOOL(agent::GetOutlineTool, "get_outline", get_outline)
REGISTER_TOOL(agent::GetProjectStatusTool, "get_project_status", get_project_status)
