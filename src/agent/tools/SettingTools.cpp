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
}

json GetSettingTool::parameters() const {
    return utils::schema::object({
        {"setting_id", utils::schema::stringProp("设定 ID")}
    }, {"setting_id"});
}
json GetSettingTool::execute(const json& args) {
    auto* s = findSetting(project_->settings, args.value("setting_id", ""));
    if (!s) return {{"error", "设定不存在"}};
    spdlog::info("[get_setting] {}", s->id);
    return json(*s);
}

json ListSettingsTool::parameters() const {
    return utils::schema::object({});
}
json ListSettingsTool::execute(const json&) {
    json arr = json::array();
    for (const auto& s : project_->settings)
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
    auto* s = findSetting(project_->settings, args.value("setting_id", ""));
    if (!s) return {{"error", "设定不存在"}};
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};

    // 指针到成员 map — 与 CharacterTools 风格一致
    using StrField = std::string Setting::*;
    static const std::map<std::string, StrField> kMap = {
        {"name", &Setting::name},
        {"category", &Setting::category},
        {"description", &Setting::description},
        {"story_function", &Setting::story_function},
        {"sensory_profile", &Setting::sensory_profile},
        {"notes", &Setting::notes},
    };

    int n = 0;
    for (auto it = f.begin(); it != f.end(); ++it) {
        auto si = kMap.find(it.key());
        if (si != kMap.end() && it.value().is_string()) {
            s->*si->second = it.value().get<std::string>();
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(*project_);
    spdlog::info("[update_setting] {} 更新 {} 个字段", s->id, n);
    return {{"success", true}, {"setting_id", s->id}, {"updated_fields", n}};
}

} // namespace agent
REGISTER_TOOL(agent::GetSettingTool, "get_setting", get_setting)
REGISTER_TOOL(agent::ListSettingsTool, "get_settings", get_settings)
REGISTER_TOOL(agent::UpdateSettingTool, "update_setting", update_setting)
