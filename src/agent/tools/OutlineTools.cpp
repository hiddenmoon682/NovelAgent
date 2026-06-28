#include "agent/tools/OutlineTools.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "utils/SchemaUtils.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

namespace {
Volume* findVolume(std::vector<Volume>& vols, const std::string& id) {
    auto it = std::find_if(vols.begin(), vols.end(), [&](const Volume& v) { return v.id == id; });
    return (it != vols.end()) ? &(*it) : nullptr;
}
PlotThread* findPlotThread(std::vector<PlotThread>& pts, const std::string& id) {
    auto it = std::find_if(pts.begin(), pts.end(), [&](const PlotThread& p) { return p.id == id; });
    return (it != pts.end()) ? &(*it) : nullptr;
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
static void validateChapterId(const std::vector<Chapter>& chapters, const std::string& id, const std::string& field, const std::string& caller) {
    if (!id.empty()) {
        auto it = std::find_if(chapters.begin(), chapters.end(), [&](const Chapter& c) { return c.id == id; });
        if (it == chapters.end()) spdlog::warn("[{}] {} 引用的章节 {} 不存在", caller, field, id);
    }
}
}

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

// ===========================================================================
// CreateVolumeTool
// ===========================================================================

json CreateVolumeTool::parameters() const {
    return utils::schema::object({
        {"title", utils::schema::stringProp("卷标题（必填）")},
        {"summary", utils::schema::stringProp("卷概要（可选）")},
        {"theme", utils::schema::stringProp("卷主题（可选）")},
        {"goal", utils::schema::stringProp("卷目标（可选）")},
        {"start_chapter_id", utils::schema::stringProp("起始章节 ID（可选）")},
        {"end_chapter_id", utils::schema::stringProp("结束章节 ID（可选）")},
        {"key_events", utils::schema::stringArrayProp("关键事件列表（可选）")},
        {"focus_characters", utils::schema::stringArrayProp("关注角色 ID 列表（可选）")},
        {"active_plot_threads", utils::schema::stringArrayProp("活跃剧情线 ID 列表（可选）")},
    }, {"title"});
}

json CreateVolumeTool::execute(const json& args) {
    std::string title = args.value("title", "");
    if (title.empty()) return {{"error", "卷标题不能为空"}};

    int max_num = 0;
    for (const auto& v : project_->outline.volumes) {
        if (v.id.size() >= 5 && v.id.substr(0, 4) == "vol-") {
            try { max_num = std::max(max_num, std::stoi(v.id.substr(4))); }
            catch (...) {}
        }
    }

    Volume vol;
    if (max_num + 1 < 10)       vol.id = "vol-00" + std::to_string(max_num + 1);
    else if (max_num + 1 < 100) vol.id = "vol-0" + std::to_string(max_num + 1);
    else                         vol.id = "vol-" + std::to_string(max_num + 1);
    vol.order = static_cast<int>(project_->outline.volumes.size()) + 1;

    vol.title             = title;
    vol.summary           = args.value("summary", "");
    vol.theme             = args.value("theme", "");
    vol.goal              = args.value("goal", "");
    vol.start_chapter_id  = args.value("start_chapter_id", "");
    validateChapterId(project_->outline.chapters, vol.start_chapter_id, "start_chapter_id", "create_volume");
    vol.end_chapter_id    = args.value("end_chapter_id", "");
    validateChapterId(project_->outline.chapters, vol.end_chapter_id, "end_chapter_id", "create_volume");
    if (args.contains("key_events") && args["key_events"].is_array())
        for (const auto& v : args["key_events"]) vol.key_events.push_back(v.get<std::string>());
    if (args.contains("focus_characters") && args["focus_characters"].is_array()) {
        for (const auto& v : args["focus_characters"]) vol.focus_characters.push_back(v.get<std::string>());
        validateIdArray(project_->characters, vol.focus_characters, "focus_characters", "create_volume");
    }
    if (args.contains("active_plot_threads") && args["active_plot_threads"].is_array()) {
        for (const auto& v : args["active_plot_threads"]) vol.active_plot_threads.push_back(v.get<std::string>());
        validateIdArray(project_->outline.plot_threads, vol.active_plot_threads, "active_plot_threads", "create_volume");
    }

    project_->outline.volumes.push_back(vol);
    ProjectIO::save(*project_);
    spdlog::info("[create_volume] {} '{}'", vol.id, title);
    return {{"success", true}, {"volume", {{"id", vol.id}, {"title", title}, {"order", vol.order}}}};
}

// ===========================================================================
// UpdateVolumeTool
// ===========================================================================

json UpdateVolumeTool::parameters() const {
    return utils::schema::object({
        {"volume_id", utils::schema::stringProp("卷 ID")},
        {"fields", utils::schema::object({}, {})}
    }, {"volume_id", "fields"});
}

json UpdateVolumeTool::execute(const json& args) {
    const std::string vid = args.value("volume_id", "");
    auto* vol = findVolume(project_->outline.volumes, vid);
    if (!vol) return {{"error", "卷不存在: " + vid}};
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};

    using StrField = std::string Volume::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"title", &Volume::title}, {"summary", &Volume::summary},
        {"theme", &Volume::theme}, {"goal", &Volume::goal},
        {"start_chapter_id", &Volume::start_chapter_id},
        {"end_chapter_id", &Volume::end_chapter_id},
    };
    using ArrField = std::vector<std::string> Volume::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"key_events", &Volume::key_events},
        {"focus_characters", &Volume::focus_characters},
        {"active_plot_threads", &Volume::active_plot_threads},
    };

    int n = 0;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const std::string& key = it.key();
        if (auto si = kStringMap.find(key); si != kStringMap.end() && it.value().is_string()) {
            vol->*si->second = it.value().get<std::string>(); n++;
        } else if (key == "order" && it.value().is_number_integer()) {
            vol->order = it.value().get<int>(); n++;
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && it.value().is_array()) {
            auto& arr = vol->*ai->second;
            arr.clear();
            for (const auto& v : it.value()) arr.push_back(v.get<std::string>());
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(*project_);
    spdlog::info("[update_volume] {} 更新 {} 个字段", vid, n);
    return {{"success", true}, {"volume_id", vid}, {"updated_fields", n}};
}

// ===========================================================================
// CreatePlotThreadTool
// ===========================================================================

json CreatePlotThreadTool::parameters() const {
    return utils::schema::object({
        {"name", utils::schema::stringProp("剧情线名称（必填）")},
        {"description", utils::schema::stringProp("描述（可选）")},
        {"type", utils::schema::stringProp("类型: main/sub/foreshadowing（可选，默认 main）")},
        {"status", utils::schema::stringProp("状态: planned/active/resolved（可选，默认 planned）")},
        {"priority", utils::schema::integerProp("优先级 0-10（可选，默认 0）")},
        {"stakes", utils::schema::stringProp("风险/赌注（可选）")},
        {"central_question", utils::schema::stringProp("中心问题（可选）")},
        {"resolution", utils::schema::stringProp("解决方案（可选）")},
        {"start_chapter_id", utils::schema::stringProp("起始章节 ID（可选）")},
        {"end_chapter_id", utils::schema::stringProp("结束章节 ID（可选）")},
        {"related_characters", utils::schema::stringArrayProp("关联角色 ID 列表（可选）")},
        {"related_settings", utils::schema::stringArrayProp("关联设定 ID 列表（可选）")},
    }, {"name"});
}

json CreatePlotThreadTool::execute(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) return {{"error", "剧情线名称不能为空"}};

    int max_num = 0;
    for (const auto& pt : project_->outline.plot_threads) {
        if (pt.id.size() >= 4 && pt.id.substr(0, 3) == "pt-") {
            try { max_num = std::max(max_num, std::stoi(pt.id.substr(3))); }
            catch (...) {}
        }
    }

    PlotThread pt;
    if (max_num + 1 < 10)       pt.id = "pt-00" + std::to_string(max_num + 1);
    else if (max_num + 1 < 100) pt.id = "pt-0" + std::to_string(max_num + 1);
    else                         pt.id = "pt-" + std::to_string(max_num + 1);

    pt.name             = name;
    pt.description      = args.value("description", "");
    pt.type             = args.value("type", "main");
    pt.status           = args.value("status", "planned");
    pt.priority         = args.value("priority", 0);
    pt.stakes           = args.value("stakes", "");
    pt.central_question = args.value("central_question", "");
    pt.resolution       = args.value("resolution", "");
    pt.start_chapter_id = args.value("start_chapter_id", "");
    validateChapterId(project_->outline.chapters, pt.start_chapter_id, "start_chapter_id", "create_plot_thread");
    pt.end_chapter_id   = args.value("end_chapter_id", "");
    validateChapterId(project_->outline.chapters, pt.end_chapter_id, "end_chapter_id", "create_plot_thread");
    if (args.contains("related_characters") && args["related_characters"].is_array()) {
        for (const auto& v : args["related_characters"]) pt.related_characters.push_back(v.get<std::string>());
        validateIdArray(project_->characters, pt.related_characters, "related_characters", "create_plot_thread");
    }
    if (args.contains("related_settings") && args["related_settings"].is_array()) {
        for (const auto& v : args["related_settings"]) pt.related_settings.push_back(v.get<std::string>());
        validateIdArray(project_->settings, pt.related_settings, "related_settings", "create_plot_thread");
    }

    project_->outline.plot_threads.push_back(pt);
    ProjectIO::save(*project_);
    spdlog::info("[create_plot_thread] {} '{}'", pt.id, name);
    return {{"success", true}, {"plot_thread", {{"id", pt.id}, {"name", name}, {"type", pt.type}}}};
}

// ===========================================================================
// UpdatePlotThreadTool
// ===========================================================================

json UpdatePlotThreadTool::parameters() const {
    return utils::schema::object({
        {"plot_thread_id", utils::schema::stringProp("剧情线 ID")},
        {"fields", utils::schema::object({}, {})}
    }, {"plot_thread_id", "fields"});
}

json UpdatePlotThreadTool::execute(const json& args) {
    const std::string pid = args.value("plot_thread_id", "");
    auto* pt = findPlotThread(project_->outline.plot_threads, pid);
    if (!pt) return {{"error", "剧情线不存在: " + pid}};
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};

    using StrField = std::string PlotThread::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"name", &PlotThread::name}, {"description", &PlotThread::description},
        {"type", &PlotThread::type}, {"status", &PlotThread::status},
        {"stakes", &PlotThread::stakes}, {"central_question", &PlotThread::central_question},
        {"resolution", &PlotThread::resolution},
        {"start_chapter_id", &PlotThread::start_chapter_id},
        {"end_chapter_id", &PlotThread::end_chapter_id},
    };
    using ArrField = std::vector<std::string> PlotThread::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"related_characters", &PlotThread::related_characters},
        {"related_settings", &PlotThread::related_settings},
    };

    int n = 0;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const std::string& key = it.key();
        if (auto si = kStringMap.find(key); si != kStringMap.end() && it.value().is_string()) {
            pt->*si->second = it.value().get<std::string>();
            if (key == "start_chapter_id") validateChapterId(project_->outline.chapters, it.value().get<std::string>(), key, "update_plot_thread");
            if (key == "end_chapter_id") validateChapterId(project_->outline.chapters, it.value().get<std::string>(), key, "update_plot_thread");
            n++;
        } else if (key == "priority" && it.value().is_number_integer()) {
            pt->priority = it.value().get<int>(); n++;
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && it.value().is_array()) {
            auto& arr = pt->*ai->second;
            arr.clear();
            for (const auto& v : it.value()) arr.push_back(v.get<std::string>());
            if (key == "related_characters") validateIdArray(project_->characters, arr, "related_characters", "update_plot_thread");
            if (key == "related_settings") validateIdArray(project_->settings, arr, "related_settings", "update_plot_thread");
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(*project_);
    spdlog::info("[update_plot_thread] {} 更新 {} 个字段", pid, n);
    return {{"success", true}, {"plot_thread_id", pid}, {"updated_fields", n}};
}

} // namespace agent

REGISTER_TOOL(agent::GetOutlineTool, "get_outline", get_outline)
REGISTER_TOOL(agent::GetProjectStatusTool, "get_project_status", get_project_status)
REGISTER_TOOL(agent::CreateVolumeTool, "create_volume", create_volume)
REGISTER_TOOL(agent::UpdateVolumeTool, "update_volume", update_volume)
REGISTER_TOOL(agent::CreatePlotThreadTool, "create_plot_thread", create_plot_thread)
REGISTER_TOOL(agent::UpdatePlotThreadTool, "update_plot_thread", update_plot_thread)
