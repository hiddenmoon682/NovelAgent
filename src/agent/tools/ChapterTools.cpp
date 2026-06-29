#include "agent/tools/ChapterTools.h"

#include "llm/TokenCounter.h"
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

Chapter* findChapter(std::vector<Chapter>& chapters,
                     const std::string& chapter_id)
{
    auto it = std::find_if(chapters.begin(), chapters.end(),
        [&](const Chapter& ch) { return ch.id == chapter_id; });
    return (it != chapters.end()) ? &(*it) : nullptr;
}

// A6: 校验单值 string ID 和 vector<string> ID 是否存在。
// 采用软校验——不存在时 warn 但不阻断（允许 LLM 先创建实体再关联）。
static void validateCharId(const std::vector<Character>& chars, const std::string& id, const std::string& field, const std::string& caller) {
    if (!id.empty()) {
        auto it = std::find_if(chars.begin(), chars.end(), [&](const Character& c) { return c.id == id; });
        if (it == chars.end()) spdlog::warn("[{}] {} 引用的角色 {} 不存在", caller, field, id);
    }
}
static void validateSettingId(const std::vector<Setting>& settings, const std::string& id, const std::string& field, const std::string& caller) {
    if (!id.empty()) {
        auto it = std::find_if(settings.begin(), settings.end(), [&](const Setting& s) { return s.id == id; });
        if (it == settings.end()) spdlog::warn("[{}] {} 引用的设定 {} 不存在", caller, field, id);
    }
}
static void validateVolumeId(const std::vector<Volume>& vols, const std::string& id, const std::string& field, const std::string& caller) {
    if (!id.empty()) {
        auto it = std::find_if(vols.begin(), vols.end(), [&](const Volume& v) { return v.id == id; });
        if (it == vols.end()) spdlog::warn("[{}] {} 引用的卷 {} 不存在", caller, field, id);
    }
}
template<typename T>
static void validateIdArray(const T& container, const std::vector<std::string>& ids, const std::string& field, const std::string& caller) {
    for (const auto& id : ids) {
        if (id.empty()) continue;
        auto it = std::find_if(container.begin(), container.end(), [&](const auto& e) { return e.id == id; });
        if (it == container.end()) spdlog::warn("[{}] {} 引用的 {} 不存在", caller, field, id);
    }
}

// A6: 校验单个 Scene 内的跨实体引用（pov_character_id / location_id / participants / plot_thread_ids）。
// 软校验——warn 不阻断，与其它 update_* 工具统一。
// 此前 UpdateChapterScenesTool 完整替换场景列表时未校验引用，可致 ID 悬空。
static void validateSceneRefs(const Project& proj, const Scene& sc, const std::string& caller) {
    validateCharId(proj.characters, sc.pov_character_id, "scene.pov_character_id", caller);
    validateSettingId(proj.settings, sc.location_id, "scene.location_id", caller);
    validateIdArray(proj.characters, sc.participants, "scene.participants", caller);
    validateIdArray(proj.outline.plot_threads, sc.plot_thread_ids, "scene.plot_thread_ids", caller);
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

    auto* ch = findChapter(project_->outline.chapters, chapter_id);
    if (!ch) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    // D1.2: 覆写前检查
    std::string existing = ProjectIO::readChapter(project_->path, ch->file_path);
    if (!existing.empty() && !project_->allow_auto_overwrite) {
        spdlog::warn("[write_chapter] {} 已有内容 ({} 字)，需确认覆写", chapter_id, existing.size());
        return {
            {"action", "confirm_overwrite"},
            {"message", "章节 " + chapter_id + " 已有 " + std::to_string(existing.size())
                      + " 字内容。如需覆写请将 allow_auto_overwrite 设为 true 后重试，"
                        "或使用 append_to_chapter 追加内容。"},
            {"preview", existing.substr(0, 200)}
        };
    }

    // B4: 覆写前自动备份旧内容，防止 LLM 一次错误 write_chapter 永久毁一章正文。
    // 备份写入 .novelagent/chapters_backup/<chapter_id>.<timestamp>.md
    if (!existing.empty()) {
        std::string backup_dir = ProjectIO::agentDir(project_->path) + "/chapters_backup";
        utils::file::createDirs(backup_dir);
        std::string ts = ProjectIO::nowTimestamp();
        for (auto& c : ts) if (c == ':') c = '-';  // 替换 Windows 文件名非法字符
        std::string backup_file = backup_dir + "/" + ch->id + "." + ts + ".md";
        utils::file::writeText(backup_file, existing);
        spdlog::info("[write_chapter] 旧内容已备份到 {} ({} 字)", backup_file, existing.size());
    }

    ProjectIO::writeChapter(project_->path, ch->file_path, content);

    // A13: 更新字数统计
    int old_count = ch->word_count;
    ch->word_count = llm::TokenCounter::estimateChineseChars(content)
                   + llm::TokenCounter::estimateEnglishWords(content);
    project_->current_word_count += (ch->word_count - old_count);
    project_->markDirty(Project::DIRTY_NOVEL | Project::DIRTY_OUTLINE);
    ProjectIO::save(*project_);
    spdlog::info("[write_chapter] {} ← {} 字节, 字数 {} → {}", chapter_id, content.size(),
                 old_count, ch->word_count);

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

    auto* ch = findChapter(project_->outline.chapters, chapter_id);
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

    // A13: 更新字数统计
    int old_count = ch->word_count;
    ch->word_count = llm::TokenCounter::estimateChineseChars(combined)
                   + llm::TokenCounter::estimateEnglishWords(combined);
    project_->current_word_count += (ch->word_count - old_count);
    project_->markDirty(Project::DIRTY_NOVEL | Project::DIRTY_OUTLINE);
    ProjectIO::save(*project_);
    spdlog::info("[append_to_chapter] {} += {} 字节, 字数 {} → {}",
                 chapter_id, append_content.size(), old_count, ch->word_count);

    return {{"success", true}, {"chapter_id", chapter_id}};

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
    // A17: 按 order 排序后输出，确保 LLM 看到有序的章节列表
    std::vector<Chapter*> sorted;
    sorted.reserve(project_->outline.chapters.size());
    for (auto& ch : project_->outline.chapters) sorted.push_back(&ch);
    std::sort(sorted.begin(), sorted.end(),
        [](const Chapter* a, const Chapter* b) { return a->order < b->order; });
    for (const auto* ch : sorted) {
        chapters.push_back({
            {"id", ch->id},
            {"title", ch->title},
            {"order", ch->order},
            {"file_path", ch->file_path},
            {"synopsis", ch->synopsis}
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
        {"synopsis", utils::schema::stringProp("章节概要用（1-2 句，可选）")},
        {"goal", utils::schema::stringProp("本章目标：主角/叙事在本章要达成的目的（可选）")},
        {"conflict", utils::schema::stringProp("核心冲突：阻碍目标实现的主要矛盾（可选）")},
        {"outcome", utils::schema::stringProp("本章结局：剧情走向的结果或 cliffhanger（可选）")},
        {"turning_point", utils::schema::stringProp("转折点：本章中剧情发生转折的事件（可选）")},
        {"hook", utils::schema::stringProp("钩子：开篇吸引读者继续阅读的悬念/爆点（可选）")},
        {"reveal", utils::schema::stringProp("揭示：本章揭露的秘密或新信息（可选）")},
        {"foreshadowing", utils::schema::stringProp("伏笔：为后续章节埋下的线索（可选）")},
        {"payoff", utils::schema::stringProp("回报：对前文伏笔的呼应/回收（可选）")},
        {"emotional_beat", utils::schema::stringProp("情感节奏：本章应有的情感基调/弧线描写（可选）")},
        {"location_id", utils::schema::stringProp("主要场景地点 ID（可选）")},
        {"time_marker", utils::schema::stringProp("时间标记，如\"三天后\"\"夜晚\"（可选）")},
        {"volume_id", utils::schema::stringProp("所属卷 ID（可选）")},
        {"status", utils::schema::stringProp("写作状态：outlined/drafting/revised/final（可选）")}
    }, {"title"});
}

json CreateChapterTool::execute(const json& args) {
    std::string title = args.value("title", "");

    if (title.empty()) {
        return {{"error", "章节标题不能为空"}};
    }

    // 计算新章节编号
    int max_order = 0;
    int max_ch_num = 0;
    for (const auto& ch : project_->outline.chapters) {
        max_order = std::max(max_order, ch.order);
        if (ch.id.size() >= 3 && ch.id.substr(0, 3) == "ch-") {
            try {
                int num = std::stoi(ch.id.substr(3));
                max_ch_num = std::max(max_ch_num, num);
            } catch (...) {}
        }
    }

    Chapter new_ch;
    // C6: 生成 ID 后校验唯一性（防御非标准 ID 格式导致的编号冲突）
    int candidate_num = max_ch_num + 1;
    std::string candidate_id = "ch-" + std::to_string(candidate_num);
    if (candidate_num < 10)      candidate_id = "ch-00" + std::to_string(candidate_num);
    else if (candidate_num < 100) candidate_id = "ch-0" + std::to_string(candidate_num);
    if (findChapter(project_->outline.chapters, candidate_id)) {
        // 编号冲突（手动创建的 ID 与自增编号重叠），递增到下一个可用编号
        while (findChapter(project_->outline.chapters, "ch-" + std::to_string(++candidate_num))) {}
        if (candidate_num < 10)      candidate_id = "ch-00" + std::to_string(candidate_num);
        else if (candidate_num < 100) candidate_id = "ch-0" + std::to_string(candidate_num);
        else                           candidate_id = "ch-" + std::to_string(candidate_num);
        spdlog::warn("[create_chapter] ID 冲突，改为 {}", candidate_id);
    }
    new_ch.id = candidate_id;
    new_ch.title = title;
    new_ch.order = max_order + 1;

    // ── 叙事核心要素 ──
    new_ch.synopsis        = args.value("synopsis", "");
    new_ch.goal            = args.value("goal", "");
    new_ch.conflict        = args.value("conflict", "");
    new_ch.outcome         = args.value("outcome", "");

    // ── 节奏与技巧 ──
    new_ch.turning_point   = args.value("turning_point", "");
    new_ch.hook            = args.value("hook", "");
    new_ch.reveal          = args.value("reveal", "");
    new_ch.foreshadowing   = args.value("foreshadowing", "");
    new_ch.payoff          = args.value("payoff", "");
    new_ch.emotional_beat  = args.value("emotional_beat", "");

    // ── 时空定位 ──
    new_ch.location_id     = args.value("location_id", "");
    validateSettingId(project_->settings, new_ch.location_id, "location_id", "create_chapter");
    new_ch.time_marker     = args.value("time_marker", "");
    new_ch.volume_id       = args.value("volume_id", "");
    validateVolumeId(project_->outline.volumes, new_ch.volume_id, "volume_id", "create_chapter");

    // ── 项目管理 ──
    new_ch.status          = args.value("status", "outlined");

    new_ch.file_path = "chapters/" + new_ch.id + ".md";

    project_->outline.chapters.push_back(new_ch);
    project_->markDirty(Project::DIRTY_OUTLINE);
    ProjectIO::save(*project_);

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

// ===========================================================================
// UpdateChapterTool — 更新章节的创作简报字段
//
// 使用字段指针白名单模式（仿 UpdateCharacterTool），
// 不在白名单中的字段会被静默忽略，避免 LLM 写入 id/order/file_path 等管理字段。
// ===========================================================================

json UpdateChapterTool::parameters() const {
    // C5: fields 列出全部可更新字段（含类型），LLM 不再靠 description 猜字段名。
    // additionalProperties=false（默认）→ 拼错字段名会被 C4 阻断并提示，而非静默忽略。
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID，例如 ch-002")},
        {"fields", utils::schema::object({
            // 字符串字段
            {"title",           utils::schema::stringProp("章节标题")},
            {"synopsis",        utils::schema::stringProp("章节概要")},
            {"goal",            utils::schema::stringProp("本章目标")},
            {"conflict",        utils::schema::stringProp("核心冲突")},
            {"outcome",         utils::schema::stringProp("本章结局")},
            {"turning_point",   utils::schema::stringProp("转折点")},
            {"hook",            utils::schema::stringProp("开篇钩子")},
            {"reveal",          utils::schema::stringProp("揭示信息")},
            {"foreshadowing",   utils::schema::stringProp("伏笔")},
            {"payoff",          utils::schema::stringProp("伏笔回收")},
            {"emotional_beat",  utils::schema::stringProp("情感节奏")},
            {"location_id",     utils::schema::stringProp("主要场景地点 ID")},
            {"time_marker",     utils::schema::stringProp("时间标记")},
            {"volume_id",       utils::schema::stringProp("所属卷 ID")},
            {"status",          utils::schema::stringProp("写作状态：outlined/drafting/revised/final")},
            // 字符串数组字段
            {"pov_characters",      utils::schema::stringArrayProp("视点角色 ID 列表")},
            {"key_events",          utils::schema::stringArrayProp("关键事件列表")},
            {"themes",              utils::schema::stringArrayProp("主题列表")},
            {"active_plot_threads", utils::schema::stringArrayProp("活跃剧情线 ID 列表")},
            {"focus_characters",    utils::schema::stringArrayProp("重点关注角色 ID 列表")},
            {"focus_settings",      utils::schema::stringArrayProp("重点关注设定 ID 列表")},
        }, {}, /*allowExtra=*/false)}
    }, {"chapter_id", "fields"});
}

json UpdateChapterTool::execute(const json& args) {
    std::string id = args.value("chapter_id", "");
    auto* ch = findChapter(project_->outline.chapters, id);
    if (!ch) {
        return {{"error", "章节 '" + id + "' 不存在"}};
    }

    const json& fields = args["fields"];
    if (!fields.is_object() || fields.empty()) {
        return {{"error", "fields 必须是非空的对象"}};
    }

    // 字符串字段白名单
    using StrField = std::string Chapter::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"title",           &Chapter::title},
        {"synopsis",        &Chapter::synopsis},
        {"goal",            &Chapter::goal},
        {"conflict",        &Chapter::conflict},
        {"outcome",         &Chapter::outcome},
        {"turning_point",   &Chapter::turning_point},
        {"hook",            &Chapter::hook},
        {"reveal",          &Chapter::reveal},
        {"foreshadowing",   &Chapter::foreshadowing},
        {"payoff",          &Chapter::payoff},
        {"emotional_beat",  &Chapter::emotional_beat},
        {"location_id",     &Chapter::location_id},
        {"time_marker",     &Chapter::time_marker},
        {"volume_id",       &Chapter::volume_id},
        {"status",          &Chapter::status},
    };

    // 字符串数组字段白名单
    using ArrField = std::vector<std::string> Chapter::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"pov_characters",      &Chapter::pov_characters},
        {"key_events",          &Chapter::key_events},
        {"themes",              &Chapter::themes},
        {"active_plot_threads", &Chapter::active_plot_threads},
        {"focus_characters",    &Chapter::focus_characters},
        {"focus_settings",      &Chapter::focus_settings},
    };

    std::vector<std::string> updated;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const std::string& key = it.key();
        const json& value = it.value();

        if (auto si = kStringMap.find(key); si != kStringMap.end() && value.is_string()) {
            ch->*si->second = value.get<std::string>();
            // A6: 校验单值 string ID 引用
            if (key == "location_id") validateSettingId(project_->settings, value.get<std::string>(), key, "update_chapter");
            if (key == "volume_id") validateVolumeId(project_->outline.volumes, value.get<std::string>(), key, "update_chapter");
            updated.push_back(key);
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && value.is_array()) {
            auto& arr = ch->*ai->second;
            arr.clear();
            for (const auto& v : value) arr.push_back(v.get<std::string>());
            // A6: 校验数组 string ID 引用
            if (key == "pov_characters") validateIdArray(project_->characters, arr, key, "update_chapter");
            if (key == "focus_characters") validateIdArray(project_->characters, arr, key, "update_chapter");
            if (key == "focus_settings") validateIdArray(project_->settings, arr, key, "update_chapter");
            if (key == "active_plot_threads") validateIdArray(project_->outline.plot_threads, arr, key, "update_chapter");
            updated.push_back(key);
        }
        // 不在白名单中的字段 → 静默忽略（包括 id/order/scenes/metadata）
    }

    if (updated.empty()) {
        return {{"error", "没有可以更新的字段。请检查字段名是否在白名单中，以及值的类型是否匹配。"}};
    }

    project_->markDirty(Project::DIRTY_OUTLINE);
    ProjectIO::save(*project_);
    // 拼接字段名用于日志
    std::string fields_str;
    for (size_t i = 0; i < updated.size(); ++i) {
        if (i > 0) fields_str += ", ";
        fields_str += updated[i];
    }
    spdlog::info("[update_chapter] {} 更新 {} 个字段: {}", id, updated.size(), fields_str);

    return {
        {"success", true},
        {"chapter", {
            {"id", ch->id},
            {"title", ch->title},
            {"updated_fields", updated}
        }}
    };
}

// ===========================================================================
// DeleteChapterTool
// ===========================================================================

json DeleteChapterTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("要删除的章节 ID")}
    }, {"chapter_id"});
}

json DeleteChapterTool::execute(const json& args) {
    const std::string cid = args.value("chapter_id", "");
    if (cid.empty()) return {{"error", "chapter_id 不能为空"}};

    auto* ch = findChapter(project_->outline.chapters, cid);
    if (!ch) return {{"error", "章节不存在: " + cid}};

    // 1) 删除 Markdown 文件
    const std::string fullPath = ProjectIO::chapterPath(project_->path, ch->file_path);
    utils::file::removeFile(fullPath);

    // 2) 从 outline.chapters 中移除
    project_->outline.chapters.erase(
        std::remove_if(project_->outline.chapters.begin(), project_->outline.chapters.end(),
            [&](const Chapter& c) { return c.id == cid; }),
        project_->outline.chapters.end());

    // 3) 级联清理
    int cascade_pt = 0, cascade_vol = 0, cascade_char = 0, cascade_dev = 0;

    // PlotThread: start_chapter_id / end_chapter_id（单值）
    for (auto& pt : project_->outline.plot_threads) {
        if (pt.start_chapter_id == cid) { pt.start_chapter_id.clear(); ++cascade_pt; }
        if (pt.end_chapter_id == cid)   { pt.end_chapter_id.clear();   ++cascade_pt; }
    }
    // Volume: start_chapter_id / end_chapter_id
    for (auto& v : project_->outline.volumes) {
        if (v.start_chapter_id == cid) { v.start_chapter_id.clear(); ++cascade_vol; }
        if (v.end_chapter_id == cid)   { v.end_chapter_id.clear();   ++cascade_vol; }
    }
    // Character: chapter_appearances（数组）
    for (auto& cr : project_->characters) {
        auto& ca = cr.chapter_appearances;
        auto before = ca.size();
        ca.erase(std::remove(ca.begin(), ca.end(), cid), ca.end());
        if (ca.size() < before) cascade_char += static_cast<int>(before - ca.size());
    }
    // CharacterDevelopment: chapter_id（单值）→ 删除该记录
    for (auto& cr : project_->characters) {
        auto before = cr.development.size();
        cr.development.erase(
            std::remove_if(cr.development.begin(), cr.development.end(),
                [&](const CharacterDevelopment& d) { return d.chapter_id == cid; }),
            cr.development.end());
        if (cr.development.size() < before) cascade_dev += static_cast<int>(before - cr.development.size());
    }

    project_->markDirty(Project::DIRTY_OUTLINE | Project::DIRTY_CHARACTERS);
    ProjectIO::save(*project_);
    spdlog::info("[delete_chapter] {} 已删除 (cascade: pt={} vol={} char={} dev={})",
                 cid, cascade_pt, cascade_vol, cascade_char, cascade_dev);
    return {
        {"success", true},
        {"deleted_id", cid},
        {"cascade", {
            {"plot_threads_cleaned", cascade_pt},
            {"volumes_cleaned", cascade_vol},
            {"character_appearances_cleaned", cascade_char},
            {"developments_cleaned", cascade_dev}
        }}
    };
}

// ===========================================================================
// UpdateChapterScenesTool
// ===========================================================================

json UpdateChapterScenesTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"scenes", utils::schema::stringArrayProp("场景对象数组（完整替换），每项含 id/title/summary/goal/conflict 等 Scene 字段")}
    }, {"chapter_id", "scenes"});
}

json UpdateChapterScenesTool::execute(const json& args) {
    auto* ch = findChapter(project_->outline.chapters, args.value("chapter_id", ""));
    if (!ch) return {{"error", "章节不存在"}};

    const auto& scenes_arr = args["scenes"];
    if (!scenes_arr.is_array()) return {{"error", "scenes 必须是数组"}};

    std::vector<Scene> parsed;
    for (const auto& s : scenes_arr) {
        if (!s.is_object()) continue;
        parsed.push_back(Scene{});
        auto& sc = parsed.back();
        sc.id             = s.value("id", "");
        sc.title          = s.value("title", "");
        sc.summary        = s.value("summary", "");
        sc.goal           = s.value("goal", "");
        sc.conflict       = s.value("conflict", "");
        sc.outcome        = s.value("outcome", "");
        sc.turning_point  = s.value("turning_point", "");
        sc.emotional_beat = s.value("emotional_beat", "");
        sc.reveal         = s.value("reveal", "");
        sc.foreshadowing  = s.value("foreshadowing", "");
        sc.payoff         = s.value("payoff", "");
        sc.pov_character_id = s.value("pov_character_id", "");
        sc.location_id    = s.value("location_id", "");
        sc.time_marker    = s.value("time_marker", "");
        if (s.contains("participants") && s["participants"].is_array())
            for (const auto& v : s["participants"]) sc.participants.push_back(v.get<std::string>());
        if (s.contains("plot_thread_ids") && s["plot_thread_ids"].is_array())
            for (const auto& v : s["plot_thread_ids"]) sc.plot_thread_ids.push_back(v.get<std::string>());
        // A6: 校验场景内的跨实体引用（pov_character_id / location_id / participants / plot_thread_ids）
        validateSceneRefs(*project_, sc, "update_chapter_scenes");
    }
    ch->scenes = std::move(parsed);
    project_->markDirty(Project::DIRTY_OUTLINE);
    ProjectIO::save(*project_);
    spdlog::info("[update_chapter_scenes] {} 更新 {} 个场景", ch->id, ch->scenes.size());
    return {{"success", true}, {"chapter_id", ch->id}, {"scene_count", ch->scenes.size()}};
}

} // namespace agent
REGISTER_TOOL(agent::ReadChapterTool, "read_chapter", read_chapter)
REGISTER_TOOL(agent::WriteChapterTool, "write_chapter", write_chapter)
REGISTER_TOOL(agent::AppendChapterTool, "append_to_chapter", append_to_chapter)
REGISTER_TOOL(agent::ListChaptersTool, "list_chapters", list_chapters)
REGISTER_TOOL(agent::CreateChapterTool, "create_chapter", create_chapter)
REGISTER_TOOL(agent::UpdateChapterTool, "update_chapter", update_chapter)
REGISTER_TOOL(agent::DeleteChapterTool, "delete_chapter", delete_chapter)
REGISTER_TOOL(agent::UpdateChapterScenesTool, "update_chapter_scenes", update_chapter_scenes)
