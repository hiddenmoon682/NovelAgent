#include "PromptContextBuilder.h"

#include "project/ProjectIO.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace prompt {
namespace {

using json = nlohmann::json;

// 在 vector<T> 中按 id 查找对象。所有 Model 类型均有 id 字段。
template<typename T>
const T* findById(const std::vector<T>& values, const std::string& id) {
    for (const auto& value : values) {
        if (value.id == id) {
            return &value;
        }
    }
    return nullptr;
}

// 判断 JSON 值是否"有意义"——非 null、非空字符串、非空数组/对象。
// 用于过滤掉序列化后无信息的字段。
bool isMeaningfulValue(const json& value) {
    if (value.is_null()) {
        return false;
    }
    if (value.is_string()) {
        return !value.get<std::string>().empty();
    }
    if (value.is_array() || value.is_object()) {
        return !value.empty();
    }
    return true;
}

// 对任意 Model 对象执行字段清理：
//   - 可选跳过 "metadata" 字段
//   - alwaysInclude 中的字段强制保留（即使为空值）
//   - 其余字段仅在有意义的值时才保留
template<typename T>
json filterObject(
    const T& object,
    bool includeMetadata,
    const std::set<std::string>& alwaysInclude = {}) {
    json raw = object;
    json filtered = json::object();

    for (auto it = raw.begin(); it != raw.end(); ++it) {
        const std::string key = it.key();
        if (key == "metadata" && !includeMetadata) {
            continue;
        }
        if (!alwaysInclude.count(key) && !isMeaningfulValue(it.value())) {
            continue;
        }
        filtered[key] = it.value();
    }

    return filtered;
}

// 检查 id 是否出现在字符串列表中（O(n) 线性查找，列表通常很小）。
bool containsId(const std::vector<std::string>& values, const std::string& id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

// 向 vector 中追加元素，通过 unordered_set 去重。
// 跳过空指针和空 id。
template<typename T>
void appendUnique(const T* value, std::vector<const T*>& out, std::unordered_set<std::string>& seen) {
    if (!value || value->id.empty()) {
        return;
    }
    if (seen.insert(value->id).second) {
        out.push_back(value);
    }
}

// 从 Project 中筛选与目标章节关联的剧情线。
//
// 选取策略（优先级递减）：
//   1. 直接取 chapter.active_plot_threads 中列出的剧情线
//   2. 若①为空（新章节或未配置），则自动匹配：
//      a) 剧情线的 related_settings 包含 chapter.location_id
//      b) 剧情线的 related_characters 包含 chapter 的 POV 人物
//   3. 超出 maxCount 时截断
std::vector<const PlotThread*> selectPlotThreads(
    const Project& project,
    const Chapter& chapter,
    std::size_t maxCount) {
    std::vector<const PlotThread*> selected;
    std::unordered_set<std::string> seen;

    for (const auto& plotId : chapter.active_plot_threads) {
        appendUnique(findById(project.outline.plot_threads, plotId), selected, seen);
    }

    if (selected.empty()) {
        for (const auto& plotThread : project.outline.plot_threads) {
            bool matches = containsId(plotThread.related_settings, chapter.location_id);
            if (!chapter.pov_characters.empty() &&
                containsId(plotThread.related_characters, chapter.pov_characters.front())) {
                matches = true;
            }
            if (matches) {
                appendUnique(&plotThread, selected, seen);
            }
        }
    }

    if (selected.size() > maxCount) {
        selected.resize(maxCount);
    }
    return selected;
}

// 从 Project 中筛选与目标章节关联的角色。
//
// 选取优先级（从高到低）：
//   1. chapter.focus_characters / pov_characters 中明确指定的角色
//   2. 各场景的 POV 人物和参与者
//   3. 关联剧情线所涉及的 related_characters
//   4. 本章有 development 记录（角色发展）的角色 → 按 enable 过滤
//   5. 本章有 chapter_appearances 出场记录的角色 → 按 enable 过滤
//   6. 超出 maxCount 时截断
//
// 这样设计确保：明确指定的角色优先保留；
// 当容量有余时再补充本章真正"有变化"的角色。
std::vector<const Character*> selectCharacters(
    const Project& project,
    const Chapter& chapter,
    const std::vector<const PlotThread*>& plotThreads,
    std::size_t maxCount) {
    std::vector<const Character*> selected;
    std::unordered_set<std::string> seen;

    auto addCharacterId = [&](const std::string& id) {
        appendUnique(findById(project.characters, id), selected, seen);
    };

    for (const auto& id : chapter.focus_characters) {
        addCharacterId(id);
    }
    for (const auto& id : chapter.pov_characters) {
        addCharacterId(id);
    }
    for (const auto& scene : chapter.scenes) {
        addCharacterId(scene.pov_character_id);
        for (const auto& participant : scene.participants) {
            addCharacterId(participant);
        }
    }
    for (const auto* plotThread : plotThreads) {
        for (const auto& id : plotThread->related_characters) {
            addCharacterId(id);
        }
    }

    // 优先：本章有角色发展记录的角色
    for (const auto& character : project.characters) {
        if (selected.size() >= maxCount) break;
        bool has_dev = false;
        for (const auto& dev : character.development) {
            if (dev.chapter_id == chapter.id) { has_dev = true; break; }
        }
        if (has_dev) appendUnique(&character, selected, seen);
    }
    // 其次：本章有出场记录的角色
    for (const auto& character : project.characters) {
        if (selected.size() >= maxCount) break;
        if (containsId(character.chapter_appearances, chapter.id)) {
            appendUnique(&character, selected, seen);
        }
    }

    if (selected.size() > maxCount) {
        selected.resize(maxCount);
    }
    return selected;
}

// 从 Project 中筛选与目标章节关联的场景/设定地点。
//
// 收集来源（按优先级）：
//   1. chapter.location_id（章节主要地点）
//   2. chapter.focus_settings（明确指定的焦点设定）
//   3. 各 scene.location_id（场景地点）
//   4. 剧情线关联的 related_settings
//   5. 超出 maxCount 时截断
std::vector<const Setting*> selectSettings(
    const Project& project,
    const Chapter& chapter,
    const std::vector<const PlotThread*>& plotThreads,
    std::size_t maxCount) {
    std::vector<const Setting*> selected;
    std::unordered_set<std::string> seen;

    auto addSettingId = [&](const std::string& id) {
        appendUnique(findById(project.settings, id), selected, seen);
    };

    addSettingId(chapter.location_id);
    for (const auto& id : chapter.focus_settings) {
        addSettingId(id);
    }
    for (const auto& scene : chapter.scenes) {
        addSettingId(scene.location_id);
    }
    for (const auto* plotThread : plotThreads) {
        for (const auto& id : plotThread->related_settings) {
            addSettingId(id);
        }
    }

    if (selected.size() > maxCount) {
        selected.resize(maxCount);
    }
    return selected;
}

// 从 Project 中筛选与所选设定地点关联的世界观规则。
//
// 选取策略：
//   1. 优先从 settings[i].related_rule_ids 直接取（正向关联）
//   2. 若①为空，则反向匹配：遍历所有启用中的规则，
//      检查其 related_settings 是否包含已选的 setting
//   3. 超出 maxCount 时截断
std::vector<const WorldRule*> selectWorldRules(
    const Project& project,
    const std::vector<const Setting*>& settings,
    std::size_t maxCount) {
    std::vector<const WorldRule*> selected;
    std::unordered_set<std::string> seen;

    for (const auto* setting : settings) {
        for (const auto& ruleId : setting->related_rule_ids) {
            appendUnique(findById(project.world_rules, ruleId), selected, seen);
        }
    }

    if (selected.empty()) {
        for (const auto& rule : project.world_rules) {
            for (const auto* setting : settings) {
                if (containsId(rule.related_settings, setting->id)) {
                    appendUnique(&rule, selected, seen);
                    break;
                }
            }
        }
    }

    if (selected.size() > maxCount) {
        selected.resize(maxCount);
    }
    return selected;
}

// 将 JSON 数组渲染为 Markdown 列表段。
// 每项取 name/title/id 作为标题，其余字段作为缩进键值对输出。
// 跳过 id/name/title 和空值，数组字段用逗号拼接。
std::string renderSectionList(const json& values, const std::string& heading) {
    if (!values.is_array() || values.empty()) {
        return {};
    }

    std::ostringstream oss;
    oss << "## " << heading << "\n";
    for (const auto& item : values) {
        const std::string title = item.value("name", item.value("title", item.value("id", "Unnamed")));
        oss << "- " << title << "\n";
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() == "id" || it.key() == "name" || it.key() == "title") {
                continue;
            }
            if (!isMeaningfulValue(it.value())) {
                continue;
            }

            if (it.value().is_string()) {
                oss << "  " << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> pieces;
                for (const auto& entry : it.value()) {
                    if (entry.is_string()) {
                        pieces.push_back(entry.get<std::string>());
                    } else {
                        pieces.push_back(entry.dump());
                    }
                }
                oss << "  " << it.key() << ": " << utils::string::join(pieces, ", ") << "\n";
            } else {
                oss << "  " << it.key() << ": " << it.value().dump() << "\n";
            }
        }
    }
    oss << "\n";
    return oss.str();
}

} // namespace

// ============================================================
// buildForChapter — 按章节构建 LLM 上下文
// ============================================================
//
// 功能：根据 PromptContextOptions 的配置，从 Project 中筛选出与
//       目标章节最相关的信息，组装为 PromptContext（含 JSON payload
//       和纯文本 rendered_prompt），供上层直接送入 LLM。
//
// 流程概览：
//   1. 校验参数（chapter_id 不能为空、章节必须在 outline 中存在）
//   2. 构建章节 order 映射 → 用于角色发展记录的时间线过滤
//   3. 查找章节所属的卷（volume）
//   4. 按 options 开关逐一填充 payload：
//      - project / style / outline / volume / chapter / scene
//      - plot_threads / characters / settings / world_rules
//   5. 若 options.include_chapter_text 开启，读取 markdown 正文
//   6. 调用 renderPrompt() 生成纯文本版本
//
// 返回：
//   - 成功 → std::optional<PromptContext>
//   - 失败（参数无效或章节不存在）→ std::nullopt
//
// ============================================================
std::optional<PromptContext> PromptContextBuilder::buildForChapter(
    const Project& project,
    const PromptContextOptions& options) {
    // ---- 1. 参数校验 ----
    if (options.chapter_id.empty()) {
        return std::nullopt;
    }

    const Chapter* chapter = findById(project.outline.chapters, options.chapter_id);
    if (!chapter) {
        return std::nullopt;
    }

    // ---- 2. 初始化 context ----
    PromptContext context;
    context.task = options.task;
    context.chapter_id = chapter->id;
    context.scene_id = options.scene_id;

    // ---- 3. 构建章节 ID → order 映射 ----
    // 用于过滤和排序角色发展记录。
    // 例如写 ch-005 时，只展示 ch-001 到 ch-005 期间发生的变化，
    // 未来章节的记录不应提前暴露给 LLM。
    std::unordered_map<std::string, int> chapterOrder;
    for (const auto& ch : project.outline.chapters) {
        chapterOrder[ch.id] = ch.order;
    }

    // order 为 0 表示未设置章节顺序（例如新项目刚创建）。此时不过滤，包含全部记录。
    const int currentOrder = chapter->order;
    const bool filterByOrder = (currentOrder > 0);

    // ---- 4. 查找章节所属的卷（volume） ----
    const Volume* volume = nullptr;
    if (!chapter->volume_id.empty()) {
        volume = findById(project.outline.volumes, chapter->volume_id);
        if (!volume) {
            context.notes.push_back("Chapter references volume_id '" + chapter->volume_id + "' but no matching Volume found.");
        }
    }

    // ---- 5. 构建 JSON payload ----
    json payload = json::object();
    payload["task"] = options.task;
    payload["chapter_id"] = chapter->id;

    // 5a. 项目级摘要（梗概、核心主题等）
    if (options.include_project_summary) {
        payload["project"] = filterObject(
            project,
            options.include_metadata,
            {"title"});
    }

    // 5b. 风格指南（文风、叙事视角等）
    if (options.include_style) {
        payload["style"] = filterObject(
            project.style,
            options.include_metadata);
    }

    // 5c. 大纲上下文（剧情线、伏笔等顶层结构）
    if (options.include_outline_context) {
        payload["outline"] = filterObject(
            project.outline,
            options.include_metadata);
    }

    // 5d. 卷纲（如果章节属于某个卷）
    if (volume) {
        payload["volume"] = filterObject(
            *volume,
            options.include_metadata,
            {"id", "title"});
    }

    // 5e. 目标章节（核心数据，always included）
    payload["chapter"] = filterObject(
        *chapter,
        options.include_metadata,
        {"id", "title"});

    // 5f. 目标场景（可选，通过 scene_id 指定）
    if (!options.scene_id.empty()) {
        const Scene* scene = findById(chapter->scenes, options.scene_id);
        if (scene) {
            payload["scene"] = filterObject(
                *scene,
                options.include_metadata,
                {"id", "title"});
        } else {
            context.notes.push_back("Requested scene_id was not found in the target chapter.");
        }
    } else if (!options.include_scenes) {
        payload["chapter"].erase("scenes");
    }

    // 5g. 关联剧情线
    // 优先使用 chapter.active_plot_threads；若为空则按地点/POV 人物自动匹配。
    const auto plotThreads = selectPlotThreads(project, *chapter, options.max_plot_threads);
    json plotThreadArray = json::array();
    for (const auto* plotThread : plotThreads) {
        plotThreadArray.push_back(filterObject(
            *plotThread,
            options.include_metadata,
            {"id", "name"}));
    }
    payload["plot_threads"] = plotThreadArray;

    // 5h. 关联角色
    // 按优先级：POV/焦点 → 场景参与者 → 剧情线关联 → 本章有发展/出场记录。
    // 每条角色数据中的 development 记录会做时间线过滤：仅保留 ≤ 当前 order 的记录。
    const auto characters = selectCharacters(project, *chapter, plotThreads, options.max_characters);
    json characterArray = json::array();
    for (const auto* character : characters) {
        json charJson = filterObject(
            *character,
            options.include_metadata,
            {"id", "name"});

        // 筛选角色发展记录：仅保留当前章节及之前发生的变化，
        // 按引用章节的顺序排列。
        charJson.erase("development");
        if (!character->development.empty()) {
            json devArray = json::array();
            for (const auto& dev : character->development) {
                if (!filterByOrder) {
                    // order 未设置时不过滤，全部包含
                    devArray.push_back(dev);
                    continue;
                }
                auto it = chapterOrder.find(dev.chapter_id);
                if (it == chapterOrder.end()) {
                    // 引用的章节在 outline 中不存在（被删除或打错 ID）
                    context.notes.push_back(
                        "Character '" + character->name + "' has a development record ('" +
                        dev.id + "') referencing chapter_id '" + dev.chapter_id +
                        "' which does not exist in the outline.");
                    continue;
                }
                if (it->second <= currentOrder) {
                    devArray.push_back(dev);
                }
                // 未来章节的记录正常过滤掉，不产生警告
            }
            if (!devArray.empty()) {
                // 按引用章节的 order 排序，确保时间线从前到后。
                std::sort(devArray.begin(), devArray.end(),
                    [&](const json& a, const json& b) {
                        return chapterOrder[a.value("chapter_id", "")] <
                               chapterOrder[b.value("chapter_id", "")];
                    });
                charJson["development"] = devArray;
            }
        }

        characterArray.push_back(charJson);
    }
    payload["characters"] = characterArray;

    // 5i. 关联场景/设定地点
    // 从 chapter.location_id + focus_settings + scenes + plotThreads 中收集。
    const auto settings = selectSettings(project, *chapter, plotThreads, options.max_settings);
    json settingArray = json::array();
    for (const auto* setting : settings) {
        settingArray.push_back(filterObject(
            *setting,
            options.include_metadata,
            {"id", "name", "category"}));
    }
    payload["settings"] = settingArray;

    // 5j. 关联世界观规则
    // 优先从 settings.related_rule_ids 取；若为空则按 setting 引用反向匹配。
    const auto worldRules = selectWorldRules(project, settings, options.max_world_rules);
    json worldRuleArray = json::array();
    for (const auto* rule : worldRules) {
        worldRuleArray.push_back(filterObject(
            *rule,
            options.include_metadata,
            {"id", "name"}));
    }
    payload["world_rules"] = worldRuleArray;

    // 5k. 章节已有正文（可选）
    // 从文件系统读取 chapter->file_path 指向的 markdown 文件，
    // 适用于修订（revise）或续写场景。
    if (options.include_chapter_text) {
        if (project.path.empty()) {
            context.notes.push_back("Project path is empty; chapter markdown could not be loaded.");
        } else if (chapter->file_path.empty()) {
            context.notes.push_back("Target chapter has no file_path; chapter markdown could not be loaded.");
        } else {
            const std::string chapterText = ProjectIO::readChapter(project.path, chapter->file_path);
            if (chapterText.empty()) {
                context.notes.push_back("Chapter markdown file is empty or missing.");
            } else {
                payload["chapter_text"] = chapterText;
            }
        }
    }

    // ---- 6. 组装最终产物 ----
    context.payload = payload;
    context.rendered_prompt = renderPrompt(context);
    return context;
}

// ============================================================
// renderPrompt — 将 PromptContext.payload（JSON）渲染为纯文本
// ============================================================
//
// 输出格式为 Markdown，按以下顺序组织各节：
//   # Task          → 任务类型
//   # Notes         → 构建过程中的提示（可选）
//   ## Project      → 项目级摘要
//   ## Style        → 风格指南
//   ## Outline      → 大纲上下文（不含 chapters/plot_threads 子数组）
//   ## Volume       → 卷纲（可选）
//   ## Target Chapter → 目标章节基本信息
//   ## Target Scene → 目标场景信息（可选）
//   ## Scenes       → 场景列表（来自 chapter.scenes）
//   ## Plot Threads → 关联剧情线
//   ## Characters   → 关联角色（含过滤后的发展记录）
//   ## Settings     → 关联设定地点
//   ## World Rules  → 关联世界观规则
//   ## Existing Chapter Draft → 已有章节正文（可选）
//
// 每节内，对象字段渲染为 "key: value" 格式，
// 数组字段用逗号拼接，跳过空值。
//
std::string PromptContextBuilder::renderPrompt(const PromptContext& context) {
    std::ostringstream oss;
    const json& payload = context.payload;

    oss << "# Task\n";
    oss << context.task << "\n\n";

    if (!context.notes.empty()) {
        oss << "# Notes\n";
        for (const auto& note : context.notes) {
            oss << "- " << note << "\n";
        }
        oss << "\n";
    }

    if (payload.contains("project") && !payload["project"].empty()) {
        oss << "## Project\n";
        for (auto it = payload["project"].begin(); it != payload["project"].end(); ++it) {
            if (it.value().is_string()) {
                oss << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> values;
                for (const auto& entry : it.value()) {
                    values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                }
                oss << it.key() << ": " << utils::string::join(values, ", ") << "\n";
            } else {
                oss << it.key() << ": " << it.value().dump() << "\n";
            }
        }
        oss << "\n";
    }

    if (payload.contains("style") && !payload["style"].empty()) {
        oss << "## Style\n";
        for (auto it = payload["style"].begin(); it != payload["style"].end(); ++it) {
            if (it.value().is_string()) {
                oss << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> values;
                for (const auto& entry : it.value()) {
                    values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                }
                oss << it.key() << ": " << utils::string::join(values, ", ") << "\n";
            }
        }
        oss << "\n";
    }

    if (payload.contains("outline") && !payload["outline"].empty()) {
        oss << "## Outline\n";
        for (auto it = payload["outline"].begin(); it != payload["outline"].end(); ++it) {
            if (it.key() == "chapters" || it.key() == "plot_threads") {
                continue;
            }
            if (it.value().is_string()) {
                oss << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> values;
                for (const auto& entry : it.value()) {
                    values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                }
                oss << it.key() << ": " << utils::string::join(values, ", ") << "\n";
            }
        }
        oss << "\n";
    }

    if (payload.contains("volume") && !payload["volume"].empty()) {
        oss << "## Volume\n";
        for (auto it = payload["volume"].begin(); it != payload["volume"].end(); ++it) {
            if (it.value().is_string()) {
                oss << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> values;
                for (const auto& entry : it.value()) {
                    values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                }
                oss << it.key() << ": " << utils::string::join(values, ", ") << "\n";
            }
        }
        oss << "\n";
    }

    if (payload.contains("chapter") && !payload["chapter"].empty()) {
        oss << "## Target Chapter\n";
        for (auto it = payload["chapter"].begin(); it != payload["chapter"].end(); ++it) {
            if (it.key() == "scenes") {
                continue;
            }
            if (it.value().is_string()) {
                oss << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> values;
                for (const auto& entry : it.value()) {
                    values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                }
                oss << it.key() << ": " << utils::string::join(values, ", ") << "\n";
            } else {
                oss << it.key() << ": " << it.value().dump() << "\n";
            }
        }
        oss << "\n";
    }

    if (payload.contains("scene") && !payload["scene"].empty()) {
        oss << "## Target Scene\n";
        for (auto it = payload["scene"].begin(); it != payload["scene"].end(); ++it) {
            if (it.value().is_string()) {
                oss << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> values;
                for (const auto& entry : it.value()) {
                    values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                }
                oss << it.key() << ": " << utils::string::join(values, ", ") << "\n";
            }
        }
        oss << "\n";
    }

    if (payload.contains("chapter") && payload["chapter"].contains("scenes") &&
        payload["chapter"]["scenes"].is_array() && !payload["chapter"]["scenes"].empty()) {
        oss << renderSectionList(payload["chapter"]["scenes"], "Scenes");
    }

    oss << renderSectionList(payload.value("plot_threads", json::array()), "Plot Threads");
    // 角色渲染：除基本字段外，追加过滤后的发展记录。
    const json& charList = payload.value("characters", json::array());
    if (!charList.empty()) {
        oss << "## Characters\n";
        for (const auto& character : charList) {
            const std::string title = character.value("name", character.value("id", "Unnamed"));
            oss << "- " << title << "\n";
            for (auto it = character.begin(); it != character.end(); ++it) {
                if (it.key() == "id" || it.key() == "name" || it.key() == "development") {
                    continue;
                }
                if (!isMeaningfulValue(it.value())) {
                    continue;
                }
                if (it.value().is_string()) {
                    oss << "  " << it.key() << ": " << it.value().get<std::string>() << "\n";
                } else if (it.value().is_array()) {
                    std::vector<std::string> values;
                    for (const auto& entry : it.value()) {
                        values.push_back(entry.is_string() ? entry.get<std::string>() : entry.dump());
                    }
                    oss << "  " << it.key() << ": " << utils::string::join(values, ", ") << "\n";
                } else {
                    oss << "  " << it.key() << ": " << it.value().dump() << "\n";
                }
            }
            // 渲染发展记录
            if (character.contains("development") && character["development"].is_array() &&
                !character["development"].empty()) {
                oss << "  发展记录:\n";
                for (const auto& dev : character["development"]) {
                    std::string devLine = "    - " + dev.value("chapter_id", "?");
                    devLine += ": " + dev.value("summary", "");
                    if (dev.contains("affected_fields") && !dev["affected_fields"].empty()) {
                        std::vector<std::string> fields;
                        for (const auto& f : dev["affected_fields"]) {
                            fields.push_back(f.get<std::string>());
                        }
                        devLine += " [" + utils::string::join(fields, ", ") + "]";
                    }
                    oss << devLine << "\n";
                }
            }
        }
        oss << "\n";
    }
    oss << renderSectionList(payload.value("settings", json::array()), "Settings");
    oss << renderSectionList(payload.value("world_rules", json::array()), "World Rules");

    if (payload.contains("chapter_text") && payload["chapter_text"].is_string()) {
        oss << "## Existing Chapter Draft\n";
        oss << payload["chapter_text"].get<std::string>() << "\n";
    }

    return oss.str();
}

} // namespace prompt
