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

// A6: 软校验——warn 不阻断
template<typename T>
static void validateIdArray(const T& container, const std::vector<std::string>& ids, const std::string& field, const std::string& caller) {
    for (const auto& id : ids) {
        if (id.empty()) continue;
        auto it = std::find_if(container.begin(), container.end(), [&](const auto& e) { return e.id == id; });
        if (it == container.end()) spdlog::warn("[{}] {} 引用的 ID {} 不存在", caller, field, id);
    }
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
    // C5: fields 列出全部可更新字段
    return utils::schema::object({
        {"setting_id", utils::schema::stringProp("设定 ID")},
        {"fields", utils::schema::object({
            {"name",                 utils::schema::stringProp("设定名称")},
            {"category",             utils::schema::stringProp("设定类别：location/object/organization/culture")},
            {"description",          utils::schema::stringProp("设定描述")},
            {"story_function",       utils::schema::stringProp("在故事中的作用/功能")},
            {"sensory_profile",      utils::schema::stringProp("感官描写指南")},
            {"notes",                utils::schema::stringProp("备注")},
            {"related_characters",   utils::schema::stringArrayProp("关联角色 ID 列表")},
            {"related_plot_threads", utils::schema::stringArrayProp("关联剧情线 ID 列表")},
            {"related_rule_ids",     utils::schema::stringArrayProp("关联世界规则 ID 列表")},
        }, {}, /*allowExtra=*/false)}
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
            if (key == "related_characters") validateIdArray(project_->characters, arr, "related_characters", "update_setting");
            if (key == "related_plot_threads") validateIdArray(project_->outline.plot_threads, arr, "related_plot_threads", "update_setting");
            if (key == "related_rule_ids") validateIdArray(project_->world_rules, arr, "related_rule_ids", "update_setting");
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    project_->markDirty(Project::DIRTY_SETTINGS);
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
    project_->markDirty(Project::DIRTY_SETTINGS);
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

// ===========================================================================
// DeleteSettingTool
// ===========================================================================

json DeleteSettingTool::parameters() const {
    return utils::schema::object({
        {"setting_id", utils::schema::stringProp("要删除的设定 ID")}
    }, {"setting_id"});
}

json DeleteSettingTool::execute(const json& args) {
    const std::string sid = args.value("setting_id", "");
    if (sid.empty()) return {{"error", "setting_id 不能为空"}};

    // 1) 从 settings 中移除
    auto it = std::find_if(project_->settings.begin(), project_->settings.end(),
        [&](const Setting& s) { return s.id == sid; });
    if (it == project_->settings.end()) return {{"error", "设定不存在: " + sid}};
    project_->settings.erase(it);

    // 2) 级联清理：各实体中引用该 setting ID 的位置
    int cascade_chapters = 0, cascade_scenes = 0, cascade_plots = 0;
    int cascade_rules = 0, cascade_other_settings = 0;

    // Chapter: location_id（单值） / focus_settings（数组）
    for (auto& ch : project_->outline.chapters) {
        if (ch.location_id == sid) { ch.location_id.clear(); ++cascade_chapters; }
        auto& fs = ch.focus_settings;
        auto before = fs.size();
        fs.erase(std::remove(fs.begin(), fs.end(), sid), fs.end());
        if (fs.size() < before) cascade_chapters += static_cast<int>(before - fs.size());
        for (auto& sc : ch.scenes) {
            if (sc.location_id == sid) { sc.location_id.clear(); ++cascade_scenes; }
        }
    }
    // PlotThread: related_settings（数组）
    for (auto& pt : project_->outline.plot_threads) {
        auto& rs = pt.related_settings;
        auto before = rs.size();
        rs.erase(std::remove(rs.begin(), rs.end(), sid), rs.end());
        if (rs.size() < before) cascade_plots += static_cast<int>(before - rs.size());
    }
    // WorldRule: related_settings（数组）
    for (auto& wr : project_->world_rules) {
        auto& rs = wr.related_settings;
        auto before = rs.size();
        rs.erase(std::remove(rs.begin(), rs.end(), sid), rs.end());
        if (rs.size() < before) cascade_rules += static_cast<int>(before - rs.size());
    }
    // 其他 Setting 的 related_rule_ids（虽不常见但保持完整性）
    for (auto& s : project_->settings) {
        auto& rr = s.related_rule_ids;
        auto before = rr.size();
        rr.erase(std::remove(rr.begin(), rr.end(), sid), rr.end());
        if (rr.size() < before) cascade_other_settings += static_cast<int>(before - rr.size());
    }

    project_->markDirty(Project::DIRTY_SETTINGS);
    ProjectIO::save(*project_);
    spdlog::info("[delete_setting] {} 已删除 (cascade: ch={} sc={} pt={} wr={} s={})",
                 sid, cascade_chapters, cascade_scenes, cascade_plots, cascade_rules, cascade_other_settings);
    return {
        {"success", true},
        {"deleted_id", sid},
        {"cascade", {
            {"chapters_cleaned", cascade_chapters + cascade_scenes},
            {"plot_threads_cleaned", cascade_plots},
            {"world_rules_cleaned", cascade_rules},
            {"other_settings_cleaned", cascade_other_settings}
        }}
    };
}

} // namespace agent
REGISTER_TOOL(agent::GetSettingTool, "get_setting", get_setting)
REGISTER_TOOL(agent::ListSettingsTool, "get_settings", get_settings)
REGISTER_TOOL(agent::UpdateSettingTool, "update_setting", update_setting)
REGISTER_TOOL(agent::CreateSettingTool, "create_setting", create_setting)
REGISTER_TOOL(agent::DeleteSettingTool, "delete_setting", delete_setting)
