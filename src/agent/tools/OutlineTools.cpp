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
        {"title", project_->title},
        {"logline", project_->logline},
        {"theme", project_->theme},
        {"target_audience", project_->target_audience},
        {"content_rating", project_->content_rating},
        {"characters_count", static_cast<int>(project_->characters.size())},
        {"chapters_count", static_cast<int>(project_->outline.chapters.size())},
        {"settings_count", static_cast<int>(project_->settings.size())},
        {"world_rules_count", static_cast<int>(project_->world_rules.size())}
    };
}

} // namespace agent

REGISTER_TOOL(agent::GetOutlineTool, "get_outline", get_outline)
REGISTER_TOOL(agent::GetProjectStatusTool, "get_project_status", get_project_status)
