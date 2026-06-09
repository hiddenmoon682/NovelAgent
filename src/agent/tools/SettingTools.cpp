#include "agent/tools/SettingTools.h"
#include "project/ProjectIO.h"
#include "utils/SchemaUtils.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

namespace {
Setting* findSetting(std::vector<Setting>& s, const std::string& id) {
    auto it = std::find_if(s.begin(), s.end(), [&](const Setting& x) { return x.id == id; });
    return (it != s.end()) ? &(*it) : nullptr;
}
} // namespace

json GetSettingTool::parameters() const {
    return utils::schema::object({
        {"setting_id", utils::schema::stringProp("设定 ID")}
    }, {"setting_id"});
}
json GetSettingTool::execute(const json& args) {
    auto* s = findSetting(project_.settings, args.value("setting_id", ""));
    if (!s) return {{"error", "设定不存在"}};
    spdlog::info("[get_setting] {}", s->id);
    return json(*s);
}

json ListSettingsTool::parameters() const {
    return utils::schema::object({});
}
json ListSettingsTool::execute(const json&) {
    json arr = json::array();
    for (const auto& s : project_.settings)
        arr.push_back({{"id", s.id}, {"name", s.name}, {"category", s.category}, {"description", s.description}});
    spdlog::info("[get_settings] 共 {} 个设定", arr.size());
    return {{"settings", std::move(arr)}};
}

json UpdateSettingTool::parameters() const {
    return utils::schema::object({
        {"setting_id", utils::schema::stringProp("设定 ID")},
        {"fields", utils::schema::object({}, {})}
    }, {"setting_id", "fields"});
}
json UpdateSettingTool::execute(const json& args) {
    auto* s = findSetting(project_.settings, args.value("setting_id", ""));
    if (!s) return {{"error", "设定不存在"}};
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};
    int n = 0;
    if (f.contains("name") && f["name"].is_string()) { s->name = f["name"]; n++; }
    if (f.contains("category") && f["category"].is_string()) { s->category = f["category"]; n++; }
    if (f.contains("description") && f["description"].is_string()) { s->description = f["description"]; n++; }
    if (f.contains("story_function") && f["story_function"].is_string()) { s->story_function = f["story_function"]; n++; }
    if (f.contains("sensory_profile") && f["sensory_profile"].is_string()) { s->sensory_profile = f["sensory_profile"]; n++; }
    if (f.contains("notes") && f["notes"].is_string()) { s->notes = f["notes"]; n++; }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(project_);
    spdlog::info("[update_setting] {} 更新 {} 个字段", s->id, n);
    return {{"success", true}, {"setting_id", s->id}, {"updated_fields", n}};
}

} // namespace agent
