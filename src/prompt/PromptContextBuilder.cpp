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

template<typename T>
const T* findById(const std::vector<T>& values, const std::string& id) {
    for (const auto& value : values) {
        if (value.id == id) {
            return &value;
        }
    }
    return nullptr;
}

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

template<typename T>
json filterObject(
    const T& object,
    const GenerationControl& generation,
    const std::vector<std::string>& tags,
    bool includeMetadata,
    const std::set<std::string>& alwaysInclude = {}) {
    json raw = object;
    json filtered = json::object();

    for (auto it = raw.begin(); it != raw.end(); ++it) {
        const std::string key = it.key();
        if (key == "generation") {
            continue;
        }
        if (key == "metadata" && !includeMetadata) {
            continue;
        }
        if (!alwaysInclude.count(key) && !shouldUseField(generation, key, tags)) {
            continue;
        }
        if (!isMeaningfulValue(it.value())) {
            continue;
        }
        filtered[key] = it.value();
    }

    return filtered;
}

bool containsId(const std::vector<std::string>& values, const std::string& id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

template<typename T>
void appendUnique(const T* value, std::vector<const T*>& out, std::unordered_set<std::string>& seen) {
    if (!value || value->id.empty()) {
        return;
    }
    if (seen.insert(value->id).second) {
        out.push_back(value);
    }
}

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
            if (!plotThread.generation.enabled) {
                continue;
            }
            if (containsId(plotThread.related_characters, chapter.pov_characters.empty() ? "" : chapter.pov_characters.front()) ||
                containsId(plotThread.related_settings, chapter.location_id)) {
                appendUnique(&plotThread, selected, seen);
            }
        }
    }

    if (selected.size() > maxCount) {
        selected.resize(maxCount);
    }
    return selected;
}

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

    for (const auto& character : project.characters) {
        if (selected.size() >= maxCount) {
            break;
        }
        if (!character.generation.enabled) {
            continue;
        }
        if (containsId(character.chapter_appearances, chapter.id)) {
            appendUnique(&character, selected, seen);
        }
    }

    if (selected.size() > maxCount) {
        selected.resize(maxCount);
    }
    return selected;
}

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
            if (!rule.generation.enabled) {
                continue;
            }
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

std::optional<PromptContext> PromptContextBuilder::buildForChapter(
    const Project& project,
    const PromptContextOptions& options) {
    if (options.chapter_id.empty()) {
        return std::nullopt;
    }

    const Chapter* chapter = findById(project.outline.chapters, options.chapter_id);
    if (!chapter) {
        return std::nullopt;
    }

    PromptContext context;
    context.task = options.task;
    context.chapter_id = chapter->id;
    context.scene_id = options.scene_id;

    // 构建章节 ID → order 映射，用于过滤和排序角色发展记录。
    // 例如写 ch-005 时，只展示 ch-001 到 ch-005 期间发生的变化。
    std::unordered_map<std::string, int> chapterOrder;
    for (const auto& ch : project.outline.chapters) {
        chapterOrder[ch.id] = ch.order;
    }

    // order 为 0 表示未设置章节顺序（例如新项目刚创建）。此时不过滤，包含全部记录。
    const int currentOrder = chapter->order;
    const bool filterByOrder = (currentOrder > 0);

    // 查找章节所属的卷纲
    const Volume* volume = nullptr;
    if (!chapter->volume_id.empty()) {
        volume = findById(project.outline.volumes, chapter->volume_id);
        if (!volume) {
            context.notes.push_back("Chapter references volume_id '" + chapter->volume_id + "' but no matching Volume found.");
        }
    }

    json payload = json::object();
    payload["task"] = options.task;
    payload["chapter_id"] = chapter->id;

    if (options.include_project_summary) {
        payload["project"] = filterObject(
            project,
            project.generation,
            project.tags,
            options.include_metadata,
            {"title"});
    }

    if (options.include_style) {
        payload["style"] = filterObject(
            project.style,
            project.style.generation,
            project.style.tags,
            options.include_metadata);
    }

    if (options.include_outline_context) {
        payload["outline"] = filterObject(
            project.outline,
            project.outline.generation,
            project.outline.tags,
            options.include_metadata);
    }

    if (volume) {
        payload["volume"] = filterObject(
            *volume,
            volume->generation,
            volume->tags,
            options.include_metadata,
            {"id", "title"});
    }

    payload["chapter"] = filterObject(
        *chapter,
        chapter->generation,
        chapter->tags,
        options.include_metadata,
        {"id", "title"});

    if (!chapter->generation.enabled) {
        context.notes.push_back("Target chapter generation is disabled; only technical identifiers were retained.");
    }

    if (!options.scene_id.empty()) {
        const Scene* scene = findById(chapter->scenes, options.scene_id);
        if (scene) {
            payload["scene"] = filterObject(
                *scene,
                scene->generation,
                scene->tags,
                options.include_metadata,
                {"id", "title"});
        } else {
            context.notes.push_back("Requested scene_id was not found in the target chapter.");
        }
    } else if (!options.include_scenes) {
        payload["chapter"].erase("scenes");
    }

    const auto plotThreads = selectPlotThreads(project, *chapter, options.max_plot_threads);
    json plotThreadArray = json::array();
    for (const auto* plotThread : plotThreads) {
        plotThreadArray.push_back(filterObject(
            *plotThread,
            plotThread->generation,
            plotThread->tags,
            options.include_metadata,
            {"id", "name"}));
    }
    payload["plot_threads"] = plotThreadArray;

    const auto characters = selectCharacters(project, *chapter, plotThreads, options.max_characters);
    json characterArray = json::array();
    for (const auto* character : characters) {
        json charJson = filterObject(
            *character,
            character->generation,
            character->tags,
            options.include_metadata,
            {"id", "name"});

        // 筛选角色发展记录：仅保留当前章节及之前发生的变化，
        // 按引用章节的顺序排列，并遵守 GenerationControl 设置。
        charJson.erase("development");
        if (!character->development.empty() &&
            shouldUseField(character->generation, "development", character->tags)) {
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

    const auto settings = selectSettings(project, *chapter, plotThreads, options.max_settings);
    json settingArray = json::array();
    for (const auto* setting : settings) {
        settingArray.push_back(filterObject(
            *setting,
            setting->generation,
            setting->tags,
            options.include_metadata,
            {"id", "name", "category"}));
    }
    payload["settings"] = settingArray;

    const auto worldRules = selectWorldRules(project, settings, options.max_world_rules);
    json worldRuleArray = json::array();
    for (const auto* rule : worldRules) {
        worldRuleArray.push_back(filterObject(
            *rule,
            rule->generation,
            rule->tags,
            options.include_metadata,
            {"id", "name"}));
    }
    payload["world_rules"] = worldRuleArray;

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

    context.payload = payload;
    context.rendered_prompt = renderPrompt(context);
    return context;
}

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
