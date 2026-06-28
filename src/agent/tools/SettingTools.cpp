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

    // 字符串字段白名单
    using StrField = std::string Setting::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"name", &Setting::name},
        {"category", &Setting::category},
        {"description", &Setting::description},
        {"story_function", &Setting::story_function},
        {"sensory_profile", &Setting::sensory_profile},
        {"notes", &Setting::notes},
    };

    // 字符串数组字段白名单
    using ArrField = std::vector<std::string> Setting::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"related_characters", &Setting::related_characters},
        {"related_plot_threads", &Setting::related_plot_threads},
        {"related_rule_ids", &Setting::related_rule_ids},
    };

    int n = 0;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const std::string& key = it.key();
        if (auto si = kStringMap.find(key); si != kStringMap.end() && it.value().is_string()) {
            s->*si->second = it.value().get<std::string>();
            n++;
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && it.value().is_array()) {
            auto& arr = s->*ai->second;
            arr.clear();
            for (const auto& v : it.value()) arr.push_back(v.get<std::string>());
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(*project_);
    spdlog::info("[update_setting] {} 更新 {} 个字段", s->id, n);
    return {{"success", true}, {"setting_id", s->id}, {"updated_fields", n}};
}

// ===========================================================================
// CreateSettingTool
// ===========================================================================

json CreateSettingTool::parameters() const {
    return utils::schema::object({
        {"name", utils::schema::stringProp("设定名称，例如 '废弃密室'")},
        {"category", utils::schema::stringProp("设定类别：location/object/organization/culture（可选）")},
        {"description", utils::schema::stringProp("设定描述（可选）")},
        {"story_function", utils::schema::stringProp("在故事中的作用/功能（可选）")},
        {"sensory_profile", utils::schema::stringProp("感官描写指南（可选）")},
        {"notes", utils::schema::stringProp("备注（可选）")}
    }, {"name"});
}

json CreateSettingTool::execute(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) return {{"error", "设定名称不能为空"}};

    // 生成 ID: setting-001 格式
    int max_num = 0;
    for (const auto& s : project_->settings) {
        if (s.id.size() >= 9 && s.id.substr(0, 8) == "setting-") {
            try { max_num = std::max(max_num, std::stoi(s.id.substr(8))); }
            catch (...) {}
        }
    }

    Setting new_s;
    if (max_num + 1 < 10)       new_s.id = "setting-00" + std::to_string(max_num + 1);
    else if (max_num + 1 < 100) new_s.id = "setting-0" + std::to_string(max_num + 1);
    else                        new_s.id = "setting-" + std::to_string(max_num + 1);

    new_s.name            = name;
    new_s.category        = args.value("category", "location");
    new_s.description     = args.value("description", "");
    new_s.story_function  = args.value("story_function", "");
    new_s.sensory_profile = args.value("sensory_profile", "");
    new_s.notes           = args.value("notes", "");

    project_->settings.push_back(new_s);
    ProjectIO::save(*project_);

    spdlog::info("[create_setting] {} '{}' (category={})", new_s.id, name, new_s.category);

    return {
        {"success", true},
        {"setting", {
            {"id", new_s.id},
            {"name", new_s.name},
            {"category", new_s.category}
        }}
    };
}

} // namespace agent
REGISTER_TOOL(agent::GetSettingTool, "get_setting", get_setting)
REGISTER_TOOL(agent::ListSettingsTool, "get_settings", get_settings)
REGISTER_TOOL(agent::UpdateSettingTool, "update_setting", update_setting)
REGISTER_TOOL(agent::CreateSettingTool, "create_setting", create_setting)
