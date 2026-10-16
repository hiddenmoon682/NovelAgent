#include "agent/tools/ChapterTools.h"

#include "llm/TokenCounter.h"
#include "project/ProjectAccess.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/IdUtils.h"
#include "utils/SchemaUtils.h"

#include <algorithm>
#include <map>
#include <set>
#include <spdlog/spdlog.h>

namespace agent {

using json = nlohmann::json;

// ===========================================================================
// 辅助: 按 ID 查找章节
// ===========================================================================

namespace {

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
// 并发下在锁外基于快照集合校验（不持锁）。
static void validateSceneRefs(const std::vector<Character>& chars,
                              const std::vector<Setting>& settings,
                              const std::vector<PlotThread>& plot_threads,
                              const Scene& sc, const std::string& caller) {
    validateCharId(chars, sc.pov_character_id, "scene.pov_character_id", caller);
    validateSettingId(settings, sc.location_id, "scene.location_id", caller);
    validateIdArray(chars, sc.participants, "scene.participants", caller);
    validateIdArray(plot_threads, sc.plot_thread_ids, "scene.plot_thread_ids", caller);
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

    // 锁内读元数据（微秒级；文件 IO 在锁外）
    std::string ch_id, title, file_path;
    const bool found = project_->withReadLock([&](const Project& p) {
        auto it = std::find_if(p.outline.chapters.begin(), p.outline.chapters.end(),
                               [&](const Chapter& c) { return c.id == chapter_id; });
        if (it == p.outline.chapters.end()) return false;
        ch_id = it->id;
        title = it->title;
        file_path = it->file_path;
        return true;
    });
    if (!found) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    std::string content = ProjectIO::readChapter(project_->path(), file_path);
    spdlog::info("[read_chapter] {} → {} 字", chapter_id, content.size());

    return {
        {"chapter_id", ch_id},
        {"title", title},
        {"content", content}
    };
}

// ===========================================================================
// WriteChapterTool
// ===========================================================================

json WriteChapterTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content", utils::schema::stringProp("章节纯文本正文（禁止 Markdown 标记，按小说正文自然分段）")}
    }, {"chapter_id", "content"});
}

json WriteChapterTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    std::string content = args.value("content", "");

    // 锁内读元数据（微秒级；文件 IO 在锁外）
    std::string file_path;
    bool allow_overwrite = false;
    const bool found = project_->withReadLock([&](const Project& p) {
        auto it = std::find_if(p.outline.chapters.begin(), p.outline.chapters.end(),
                               [&](const Chapter& c) { return c.id == chapter_id; });
        if (it == p.outline.chapters.end()) return false;
        file_path = it->file_path;
        allow_overwrite = p.allow_auto_overwrite;
        return true;
    });
    if (!found) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    // ── 锁外：文件 IO（读/备份/写）与字数计算（不持锁）──

    // D1.2: 覆写前检查
    std::string existing = ProjectIO::readChapter(project_->path(), file_path);
    if (!existing.empty() && !allow_overwrite) {
        spdlog::warn("[write_chapter] {} 已有内容 ({} 字)，需确认覆写", chapter_id, existing.size());
        return {
            {"action", "confirm_overwrite"},
            {"message", "章节 " + chapter_id + " 已有 " + std::to_string(existing.size())
                      + " 字内容。覆写保护为项目级配置，模型无法通过任何参数"
                        "修改。可改用 append_to_chapter 追加内容；确需覆写时，"
                        "请向用户说明需在项目配置中开启覆写。"},
            {"preview", existing.substr(0, 200)}
        };
    }

    // B4: 覆写前自动备份旧内容，防止 LLM 一次错误 write_chapter 永久毁一章正文。
    // 备份写入 .novelagent/chapters_backup/<chapter_id>.<timestamp>.md
    if (!existing.empty()) {
        std::string backup_dir = ProjectIO::agentDir(project_->path()) + "/chapters_backup";
        utils::file::createDirs(backup_dir);
        std::string ts = ProjectIO::nowTimestamp();
        for (auto& c : ts) if (c == ':') c = '-';  // 替换 Windows 文件名非法字符
        std::string backup_file = backup_dir + "/" + chapter_id + "." + ts + ".md";
        utils::file::writeText(backup_file, existing);
        spdlog::info("[write_chapter] 旧内容已备份到 {} ({} 字)", backup_file, existing.size());
    }

    ProjectIO::writeChapter(project_->path(), file_path, content);

    // A13: 更新字数统计（锁内小改）
    const int new_count = llm::TokenCounter::estimateChineseChars(content)
                        + llm::TokenCounter::estimateEnglishWords(content);
    int old_count = 0;
    project_->withWriteLock([&](Project& p) {
        auto it = std::find_if(p.outline.chapters.begin(), p.outline.chapters.end(),
                               [&](const Chapter& c) { return c.id == chapter_id; });
        if (it == p.outline.chapters.end()) return;
        old_count = it->word_count;
        it->word_count = new_count;
        p.current_word_count += (new_count - old_count);
        p.markDirty(Project::DIRTY_NOVEL | Project::DIRTY_OUTLINE);
    });
    project_->save();
    spdlog::info("[write_chapter] {} ← {} 字节, 字数 {} → {}", chapter_id, content.size(),
                 old_count, new_count);

    return {{"success", true}, {"chapter_id", chapter_id}};
}

// ===========================================================================
// AppendChapterTool
// ===========================================================================

json AppendChapterTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"content", utils::schema::stringProp("要追加的纯文本正文（禁止 Markdown 标记）")}
    }, {"chapter_id", "content"});
}

json AppendChapterTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    std::string append_content = args.value("content", "");

    // 锁内读元数据（微秒级；文件 IO 在锁外）
    std::string file_path;
    const bool found = project_->withReadLock([&](const Project& p) {
        auto it = std::find_if(p.outline.chapters.begin(), p.outline.chapters.end(),
                               [&](const Chapter& c) { return c.id == chapter_id; });
        if (it == p.outline.chapters.end()) return false;
        file_path = it->file_path;
        return true;
    });
    if (!found) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    // ── 锁外：文件 IO（读现有内容 → 追加 → 写回）与字数计算（不持锁）──
    std::string existing = ProjectIO::readChapter(project_->path(), file_path);
    if (!existing.empty() && existing.back() != '\n') {
        existing += '\n';
    }
    std::string combined = existing + append_content;
    if (combined.back() != '\n') {
        combined += '\n';
    }

    ProjectIO::writeChapter(project_->path(), file_path, combined);

    // A13: 更新字数统计（锁内小改）
    const int new_count = llm::TokenCounter::estimateChineseChars(combined)
                        + llm::TokenCounter::estimateEnglishWords(combined);
    int old_count = 0;
    project_->withWriteLock([&](Project& p) {
        auto it = std::find_if(p.outline.chapters.begin(), p.outline.chapters.end(),
                               [&](const Chapter& c) { return c.id == chapter_id; });
        if (it == p.outline.chapters.end()) return;
        old_count = it->word_count;
        it->word_count = new_count;
        p.current_word_count += (new_count - old_count);
        p.markDirty(Project::DIRTY_NOVEL | Project::DIRTY_OUTLINE);
    });
    project_->save();
    spdlog::info("[append_to_chapter] {} += {} 字节, 字数 {} → {}",
                 chapter_id, append_content.size(), old_count, new_count);

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
    auto snapshot = project_->getOutline().chapters;  // 读快照（共享锁，锁外计算）
    std::vector<Chapter*> sorted;
    sorted.reserve(snapshot.size());
    for (auto& ch : snapshot) sorted.push_back(&ch);
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

    // 锁外计算：读快照 → 编号/查重/构造/软校验（不持锁）
    const Outline outline = project_->getOutline();
    const auto settings = project_->getSettings();

    // 计算新章节编号
    int max_order = 0;
    int max_ch_num = 0;
    for (const auto& ch : outline.chapters) {
        max_order = std::max(max_order, ch.order);
        // D2: 非标准 ID（如 ch-abc）解析失败时安全跳过，不参与编号统计
        if (auto num = utils::id::tryParseIdNumber(ch.id, "ch-")) {
            max_ch_num = std::max(max_ch_num, *num);
        }
    }

    Chapter new_ch;
    // C6: 生成 ID 后校验唯一性（防御非标准 ID 格式导致的编号冲突）
    int candidate_num = max_ch_num + 1;
    std::string candidate_id = utils::id::formatSequentialId("ch-", candidate_num);
    auto find_in = [&](const std::string& id) {
        return std::find_if(outline.chapters.begin(), outline.chapters.end(),
                            [&](const Chapter& c) { return c.id == id; })
               != outline.chapters.end();
    };
    if (find_in(candidate_id)) {
        // 编号冲突（手动创建的 ID 与自增编号重叠），递增到下一个可用编号
        // WHY: 查重必须用补零格式（ch-005）——存量 ID 均为补零格式，
        // 此前用未补零的 "ch-" + std::to_string() 查重永远匹配不到，冲突检测形同虚设（D1）
        // WHY: 本分支为防御性代码，正常编号扫描下不可达——存量补零 ID 必然被
        // tryParseIdNumber 解析计入 max，候选 pad(max+1) 不会与之重叠；
        // 保留以防未来 ID 格式规则变化
        do {
            candidate_id = utils::id::formatSequentialId("ch-", ++candidate_num);
        } while (find_in(candidate_id));
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
    validateSettingId(settings, new_ch.location_id, "location_id", "create_chapter");
    new_ch.time_marker     = args.value("time_marker", "");
    new_ch.volume_id       = args.value("volume_id", "");
    validateVolumeId(outline.volumes, new_ch.volume_id, "volume_id", "create_chapter");

    // ── 项目管理 ──
    new_ch.status          = args.value("status", "outlined");

    new_ch.file_path = "chapters/" + new_ch.id + ".md";

    project_->addChapter(new_ch);
    project_->save();

    // 初始化正文为纯文本标题（正文非 Markdown，不得写 "# 标题" 这类标记头）
    std::string init_content = title + "\n\n";
    ProjectIO::writeChapter(project_->path(), new_ch.file_path, init_content);

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
    const json& fields = args["fields"];
    if (!fields.is_object() || fields.empty()) {
        return {{"error", "fields 必须是非空的对象"}};
    }

    // 锁外取校验快照（软校验用，不持锁）
    const auto settings = project_->getSettings();
    const auto volumes = project_->getOutline().volumes;
    const auto characters = project_->getCharacters();
    const auto plot_threads = project_->getOutline().plot_threads;

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

    // 锁内小改：逐字段赋值 + 软校验（updateChapter 自动 markDirty）
    std::vector<std::string> updated;
    std::string result_id, result_title;
    const bool ok = project_->updateChapter(id, [&](Chapter& ch) {
        result_id = ch.id;
        result_title = ch.title;
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const std::string& key = it.key();
            const json& value = it.value();

            if (auto si = kStringMap.find(key); si != kStringMap.end() && value.is_string()) {
                ch.*si->second = value.get<std::string>();
                if (key == "title") result_title = value.get<std::string>();
                // A6: 校验单值 string ID 引用
                if (key == "location_id") validateSettingId(settings, value.get<std::string>(), key, "update_chapter");
                if (key == "volume_id") validateVolumeId(volumes, value.get<std::string>(), key, "update_chapter");
                updated.push_back(key);
            } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && value.is_array()) {
                auto& arr = ch.*ai->second;
                arr.clear();
                for (const auto& v : value) arr.push_back(v.get<std::string>());
                // A6: 校验数组 string ID 引用
                if (key == "pov_characters") validateIdArray(characters, arr, key, "update_chapter");
                if (key == "focus_characters") validateIdArray(characters, arr, key, "update_chapter");
                if (key == "focus_settings") validateIdArray(settings, arr, key, "update_chapter");
                if (key == "active_plot_threads") validateIdArray(plot_threads, arr, key, "update_chapter");
                updated.push_back(key);
            }
            // 白名单外的字段在这里被忽略（包括 id/order/scenes/metadata）。正常路径上管线层的
            // 递归校验已会先拒掉未知键；这里的忽略是兜底——tool_search 当轮加载的工具不在
            // schema 缓存里，校验会被整段跳过（见 ToolPipeline 的 schema_cache_ 注释）。
        }
    });
    if (!ok) {
        return {{"error", "章节 '" + id + "' 不存在"}};
    }

    if (updated.empty()) {
        return {{"error", "没有可以更新的字段。请检查字段名是否在白名单中，以及值的类型是否匹配。"}};
    }

    project_->save();
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
            {"id", result_id},
            {"title", result_title},
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

    // 锁内级联删除（跨聚合：outline + characters，一次独占锁完成；文件路径锁内取出）
    std::string file_path;
    int cascade_pt = 0, cascade_vol = 0, cascade_char = 0, cascade_dev = 0;
    const bool ok = project_->withWriteLock([&](Project& p) {
        auto it = std::find_if(p.outline.chapters.begin(), p.outline.chapters.end(),
                               [&](const Chapter& c) { return c.id == cid; });
        if (it == p.outline.chapters.end()) return false;
        file_path = it->file_path;
        p.outline.chapters.erase(it);

        // PlotThread: start_chapter_id / end_chapter_id（单值）
        for (auto& pt : p.outline.plot_threads) {
            if (pt.start_chapter_id == cid) { pt.start_chapter_id.clear(); ++cascade_pt; }
            if (pt.end_chapter_id == cid)   { pt.end_chapter_id.clear();   ++cascade_pt; }
        }
        // Volume: start_chapter_id / end_chapter_id
        for (auto& v : p.outline.volumes) {
            if (v.start_chapter_id == cid) { v.start_chapter_id.clear(); ++cascade_vol; }
            if (v.end_chapter_id == cid)   { v.end_chapter_id.clear();   ++cascade_vol; }
        }
        // Character: chapter_appearances（数组）
        for (auto& cr : p.characters) {
            auto& ca = cr.chapter_appearances;
            auto before = ca.size();
            ca.erase(std::remove(ca.begin(), ca.end(), cid), ca.end());
            if (ca.size() < before) cascade_char += static_cast<int>(before - ca.size());
        }
        // CharacterDevelopment: chapter_id（单值）→ 删除该记录
        for (auto& cr : p.characters) {
            auto before = cr.development.size();
            cr.development.erase(
                std::remove_if(cr.development.begin(), cr.development.end(),
                    [&](const CharacterDevelopment& d) { return d.chapter_id == cid; }),
                cr.development.end());
            if (cr.development.size() < before) cascade_dev += static_cast<int>(before - cr.development.size());
        }
        p.markDirty(Project::DIRTY_OUTLINE | Project::DIRTY_CHARACTERS);
        return true;
    });
    if (!ok) return {{"error", "章节不存在: " + cid}};

    // 锁外：删除 Markdown 文件
    utils::file::removeFile(ProjectIO::chapterPath(project_->path(), file_path));

    project_->save();
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

// 扫出场景列表里 "sc-NNN" 形式 ID 的最大号（非标准 ID 安全跳过，不参与编号统计）。
static int maxSceneIdNumber(const std::vector<Scene>& scenes) {
    int max_num = 0;
    for (const auto& sc : scenes) {
        if (auto num = utils::id::tryParseIdNumber(sc.id, "sc-")) {
            max_num = std::max(max_num, *num);
        }
    }
    return max_num;
}

// 场景 ID 规整：保证本章内 ID 非空且唯一。
//
// 为什么改由程序接管：场景 ID 此前完全由模型填写（可空、可重复、跨轮次还会漂移），
// 而 ID 是按场景定位的唯一依据——ID 不可信时，按 ID 定位会打空或打错（且这类错误
// 不是格式错误，校验器拦不住）。编号思路与 create_chapter 一致（扫 max 号 + 冲突递增）：
//   ① 带了 ID 且不与前面重复 → 原样保留（不重新编号，否则改动一项会让其它场景 ID 漂移）；
//   ② 空 ID → 补下一个可用号；
//   ③ 与前面重复 → 改写为新号并记日志。
// seed 必须同时覆盖"本章原有场景"与"本次传入场景"的最大号，否则补号会与旧号重叠。
static void normalizeSceneIds(std::vector<Scene>& scenes, int seed) {
    std::set<std::string> used;
    int next = seed;
    for (auto& sc : scenes) {
        if (!sc.id.empty() && used.insert(sc.id).second) continue;
        if (!sc.id.empty()) {
            spdlog::warn("[update_chapter_scenes] 场景 ID '{}' 在本章内重复，改写为新号", sc.id);
        }
        sc.id = utils::id::formatSequentialId("sc-", ++next);
        used.insert(sc.id);
    }
}

// scenes 元素的嵌套 schema。
// 为什么必须显式声明为 object：实现逐项按 Scene 结构体取值，模型只能从 schema
// 得知可填哪些字段；此前用 stringArrayProp 声明成"字符串数组"，与实际接受格式
// 相反——模型照 schema 提交字符串数组时会被实现逐项跳过（见 execute 里的拦截）。
static json sceneItemSchema() {
    return utils::schema::object({
        {"id",               utils::schema::stringProp("场景 ID；新建场景可留空（系统会补 sc-N），"
                                                      "修改已有场景请原样回传以保持 ID 稳定")},
        {"title",            utils::schema::stringProp("场景标题")},
        {"summary",          utils::schema::stringProp("场景摘要")},
        {"goal",             utils::schema::stringProp("场景目标")},
        {"conflict",         utils::schema::stringProp("场景冲突")},
        {"outcome",          utils::schema::stringProp("场景结果")},
        {"turning_point",    utils::schema::stringProp("转折点")},
        {"emotional_beat",   utils::schema::stringProp("情绪节拍")},
        {"reveal",           utils::schema::stringProp("揭示的信息")},
        {"foreshadowing",    utils::schema::stringProp("埋设的伏笔")},
        {"payoff",           utils::schema::stringProp("回收的伏笔")},
        {"pov_character_id", utils::schema::stringProp("视点角色 ID")},
        {"location_id",      utils::schema::stringProp("地点设定 ID")},
        {"time_marker",      utils::schema::stringProp("时间标记")},
        {"participants",     utils::schema::stringArrayProp("出场角色 ID 列表")},
        {"plot_thread_ids",  utils::schema::stringArrayProp("关联剧情线 ID 列表")}
    }, {}, /*allowExtra=*/true);
    // allowExtra=true 的理由：Scene 的 to_json 总会输出 metadata（且 from_json 会把未知键收进
    // metadata），而"保住 ID"要求模型把读到的场景对象原样回传；若这里设为 false，回传里的
    // metadata 会被判成未知字段而整次拒绝。未声明的键由实现忽略（本工具不解析 metadata，
    // 故也刻意不把 metadata 写进 properties，以免模型以为可写）。
}

json UpdateChapterScenesTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"scenes", utils::schema::objectArrayProp(
            "场景列表，完整替换本章全部场景；传空数组表示清空。每项为一个场景对象",
            sceneItemSchema())}
    }, {"chapter_id", "scenes"});
}

json UpdateChapterScenesTool::execute(const json& args) {
    const std::string chapter_id = args.value("chapter_id", "");
    const auto& scenes_arr = args["scenes"];
    if (!scenes_arr.is_array()) return {{"error", "scenes 必须是数组"}};

    // 锁外取校验快照（软校验用，不持锁）
    const auto characters = project_->getCharacters();
    const auto settings = project_->getSettings();
    const auto outline = project_->getOutline();
    const auto plot_threads = outline.plot_threads;

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
        validateSceneRefs(characters, settings, plot_threads, sc, "update_chapter_scenes");
    }

    // 元素解析不全时的处置（本工具是"整体替换"，下面这段决定坏输入会不会毁掉整表）：
    //   ① 非空输入却一条都没解析出来 → 拒绝。这是"输入坏了"的确证——照旧执行会把
    //      整章场景清空（修复前正是如此，还回报 success）。传空数组才是明确的清空意图。
    //   ② 部分解析成功 → 写入合法项，但把被跳过的项逐条回报，避免"模型以为全写上了"。
    json skipped = json::array();
    if (parsed.size() != scenes_arr.size()) {
        for (size_t i = 0; i < scenes_arr.size(); ++i) {
            if (!scenes_arr[i].is_object()) {
                skipped.push_back({{"index", i},
                                   {"received_type", scenes_arr[i].type_name()},
                                   {"reason", "不是对象；每项须为场景对象（含 title/summary/"
                                              "goal/conflict 等字段）"}});
            }
        }
        if (parsed.empty()) {
            return {{"error", "scenes 的 " + std::to_string(scenes_arr.size()) +
                              " 项都不是对象，已拒绝执行以免清空本章原有场景"},
                    {"skipped", skipped}};
        }
    }

    // 场景 ID 由程序接管（规则见 normalizeSceneIds）。种子取"本章原有场景"与"本次传入
    // 场景"两者的最大号，保证补出的号既不与旧号重叠，也不与传入的号重叠。
    int id_seed = maxSceneIdNumber(parsed);
    for (const auto& ch : outline.chapters) {
        if (ch.id == chapter_id) {
            id_seed = std::max(id_seed, maxSceneIdNumber(ch.scenes));
            break;
        }
    }
    normalizeSceneIds(parsed, id_seed);

    // 锁内小改：整体替换场景列表（updateChapter 自动 markDirty）
    std::string result_id;
    size_t scene_count = 0;
    const bool ok = project_->updateChapter(chapter_id, [&](Chapter& ch) {
        result_id = ch.id;
        ch.scenes = std::move(parsed);
        scene_count = ch.scenes.size();
    });
    if (!ok) return {{"error", "章节不存在"}};
    project_->save();
    spdlog::info("[update_chapter_scenes] {} 更新 {} 个场景", result_id, scene_count);
    json result = {{"success", true}, {"chapter_id", result_id}, {"scene_count", scene_count}};
    if (!skipped.empty()) {
        result["skipped"] = skipped;
        result["warning"] = "有 " + std::to_string(skipped.size()) +
                            " 项格式不合法已被跳过；本工具为整体替换，本章场景现为 " +
                            std::to_string(scene_count) + " 个";
    }
    return result;
}

// update_scene 的 fields 白名单 schema。
// 刻意**不含 id**：场景 ID 由程序维护（空则补号、重名改写，见 normalizeSceneIds），
// 允许模型改 ID 会让"按 ID 定位"重新变得不可信；白名单外字段会被递归校验直接拦下。
static json sceneEditableFieldsSchema() {
    return utils::schema::object({
        {"title",            utils::schema::stringProp("场景标题")},
        {"summary",          utils::schema::stringProp("场景摘要")},
        {"goal",             utils::schema::stringProp("场景目标")},
        {"conflict",         utils::schema::stringProp("场景冲突")},
        {"outcome",          utils::schema::stringProp("场景结果")},
        {"turning_point",    utils::schema::stringProp("转折点")},
        {"emotional_beat",   utils::schema::stringProp("情绪节拍")},
        {"reveal",           utils::schema::stringProp("揭示的信息")},
        {"foreshadowing",    utils::schema::stringProp("埋设的伏笔")},
        {"payoff",           utils::schema::stringProp("回收的伏笔")},
        {"pov_character_id", utils::schema::stringProp("视点角色 ID")},
        {"location_id",      utils::schema::stringProp("地点设定 ID")},
        {"time_marker",      utils::schema::stringProp("时间标记")},
        {"participants",     utils::schema::stringArrayProp("出场角色 ID 列表")},
        {"plot_thread_ids",  utils::schema::stringArrayProp("关联剧情线 ID 列表")}
    });
}

json UpdateSceneTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID")},
        {"scene_id",   utils::schema::stringProp("要修改的场景 ID（先用 get_chapter_context 查得，勿臆造）")},
        {"fields",     sceneEditableFieldsSchema()}
    }, {"chapter_id", "scene_id", "fields"});
}

json UpdateSceneTool::execute(const json& args) {
    const std::string chapter_id = args.value("chapter_id", "");
    const std::string scene_id   = args.value("scene_id", "");
    if (chapter_id.empty()) return {{"error", "chapter_id 不能为空"}};
    if (scene_id.empty())   return {{"error", "scene_id 不能为空"}};
    // 必须先 contains 再取：const operator[] 在缺键时是 assert/UB（Release 带 NDEBUG 会静默
    // 解引用 end()）。正常管线有 required 校验兜底，但 tool_search 中途加载的工具当轮不在校验
    // 缓存内、会被整段跳过，故这里不能依赖校验器一定跑过。
    if (!args.contains("fields")) return {{"error", "fields 必须是非空的对象"}};
    const json& fields = args["fields"];
    if (!fields.is_object() || fields.empty()) return {{"error", "fields 必须是非空的对象"}};

    // 锁外取校验快照（软校验用，不持锁）
    const auto characters = project_->getCharacters();
    const auto settings = project_->getSettings();
    const auto plot_threads = project_->getOutline().plot_threads;

    // 字段白名单：13 个字符串 + 2 个字符串数组（id 与 metadata 不在其中）
    using StrField = std::string Scene::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"title",            &Scene::title},
        {"summary",          &Scene::summary},
        {"goal",             &Scene::goal},
        {"conflict",         &Scene::conflict},
        {"outcome",          &Scene::outcome},
        {"turning_point",    &Scene::turning_point},
        {"emotional_beat",   &Scene::emotional_beat},
        {"reveal",           &Scene::reveal},
        {"foreshadowing",    &Scene::foreshadowing},
        {"payoff",           &Scene::payoff},
        {"pov_character_id", &Scene::pov_character_id},
        {"location_id",      &Scene::location_id},
        {"time_marker",      &Scene::time_marker},
    };
    using ArrField = std::vector<std::string> Scene::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"participants",     &Scene::participants},
        {"plot_thread_ids",  &Scene::plot_thread_ids},
    };

    // 锁内小改：只动命中的那个场景（updateChapter 自动 markDirty）
    std::vector<std::string> updated;
    std::string result_id;
    // 命中条数：ID 由程序接管后应当唯一，但**历史数据**可能带着重复 ID 或空 ID 载入
    // （from_json 不做规整），此时按歧义处理、一条都不改，而不是"改第一条却回报 success"。
    size_t matches = 0;
    const bool ok = project_->updateChapter(chapter_id, [&](Chapter& ch) {
        result_id = ch.id;
        matches = static_cast<size_t>(std::count_if(ch.scenes.begin(), ch.scenes.end(),
                    [&](const Scene& s) { return s.id == scene_id; }));
        if (matches != 1) return;
        for (auto& sc : ch.scenes) {
            if (sc.id != scene_id) continue;
            for (auto it = fields.begin(); it != fields.end(); ++it) {
                const std::string& key = it.key();
                const json& value = it.value();
                if (auto si = kStringMap.find(key); si != kStringMap.end() && value.is_string()) {
                    sc.*si->second = value.get<std::string>();
                    updated.push_back(key);
                } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && value.is_array()) {
                    auto& arr = sc.*ai->second;
                    arr.clear();
                    // 跳过非字符串元素而不是抛 type_error：单个坏元素不该让整次调用炸掉，
                    // 且异常文案不会指明是哪一项（校验被跳过时这条路径实际可达）。
                    for (const auto& v : value) {
                        if (v.is_string()) arr.push_back(v.get<std::string>());
                    }
                    updated.push_back(key);
                }
                // 白名单外或类型不符 → 不计入 updated；只要有任一字段生效就返回 success，
                // 未生效的字段名不会出现在 updated_fields 里（全部无效时才整次报错）。
            }
            // A6: 场景内跨实体引用软校验（warn 不阻断，与 update_chapter_scenes 保持一致）
            validateSceneRefs(characters, settings, plot_threads, sc, "update_scene");
            break;
        }
    });
    if (!ok) return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    if (matches == 0) {
        return {{"error", "场景 '" + scene_id + "' 不在本章中。请先用 get_chapter_context "
                          "查得真实场景 ID，勿臆造"}};
    }
    if (matches > 1) {
        return {{"error", "本章有 " + std::to_string(matches) + " 个场景共用 ID '" + scene_id
                          + "'，无法确定要改哪一个。请用 update_chapter_scenes 整体替换以修正重复 ID"}};
    }
    if (updated.empty()) {
        return {{"error", "没有可修改的字段（fields 只接受场景自身字段，且 id 由程序维护不可修改）"}};
    }

    project_->save();
    std::string fields_str;
    for (size_t i = 0; i < updated.size(); ++i) {
        if (i > 0) fields_str += ", ";
        fields_str += updated[i];
    }
    spdlog::info("[update_scene] {}/{} 更新 {} 个字段: {}",
                 result_id, scene_id, updated.size(), fields_str);

    return {
        {"success", true},
        {"chapter_id", result_id},
        {"scene_id", scene_id},
        {"updated_fields", updated}
    };
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
REGISTER_TOOL(agent::UpdateSceneTool, "update_scene", update_scene)
