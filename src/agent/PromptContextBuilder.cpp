#include "PromptContextBuilder.h"
#include "agent/PromptSelector.h"

#include "project/ProjectIO.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace prompt {

using json = nlohmann::json;
using namespace selector;

// ============================================================
// buildForChapter — 按章节构建 LLM 上下文
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
    std::unordered_map<std::string, int> chapterOrder;
    for (const auto& ch : project.outline.chapters) {
        chapterOrder[ch.id] = ch.order;
    }

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

    // 5a. 项目级摘要
    if (options.include_project_summary) {
        payload["project"] = filterObject(project, options.include_metadata, {"title"});
    }

    // 5b. 风格指南
    if (options.include_style) {
        payload["style"] = filterObject(project.style, options.include_metadata);
    }

    // 5c. 大纲上下文
    if (options.include_outline_context) {
        payload["outline"] = filterObject(project.outline, options.include_metadata);
    }

    // 5d. 卷纲
    if (volume) {
        payload["volume"] = filterObject(*volume, options.include_metadata, {"id", "title"});
    }

    // 5e. 目标章节
    payload["chapter"] = filterObject(*chapter, options.include_metadata, {"id", "title"});

    // 5f. 目标场景
    if (!options.scene_id.empty()) {
        const Scene* scene = findById(chapter->scenes, options.scene_id);
        if (scene) {
            payload["scene"] = filterObject(*scene, options.include_metadata, {"id", "title"});
        } else {
            context.notes.push_back("Requested scene_id was not found in the target chapter.");
        }
    } else if (!options.include_scenes) {
        payload["chapter"].erase("scenes");
    }

    // 5g. 关联剧情线
    const auto plotThreads = selectPlotThreads(project, *chapter, options.max_plot_threads);
    json plotThreadArray = json::array();
    for (const auto* plotThread : plotThreads) {
        plotThreadArray.push_back(filterObject(*plotThread, options.include_metadata, {"id", "name"}));
    }
    payload["plot_threads"] = plotThreadArray;

    // 5h. 关联角色（含 development 时间线过滤）
    const auto characters = selectCharacters(project, *chapter, plotThreads, options.max_characters);
    json characterArray = json::array();
    for (const auto* character : characters) {
        json charJson = filterObject(*character, options.include_metadata, {"id", "name"});

        charJson.erase("development");
        if (!character->development.empty()) {
            json devArray = json::array();
            for (const auto& dev : character->development) {
                if (!filterByOrder) {
                    devArray.push_back(dev);
                    continue;
                }
                auto it = chapterOrder.find(dev.chapter_id);
                if (it == chapterOrder.end()) {
                    context.notes.push_back(
                        "Character '" + character->name + "' has a development record ('" +
                        dev.id + "') referencing chapter_id '" + dev.chapter_id +
                        "' which does not exist in the outline.");
                    continue;
                }
                if (it->second <= currentOrder) {
                    devArray.push_back(dev);
                }
            }
            if (!devArray.empty()) {
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
    const auto settings = selectSettings(project, *chapter, plotThreads, options.max_settings);
    json settingArray = json::array();
    for (const auto* setting : settings) {
        settingArray.push_back(filterObject(*setting, options.include_metadata, {"id", "name", "category"}));
    }
    payload["settings"] = settingArray;

    // 5j. 关联世界观规则
    const auto worldRules = selectWorldRules(project, settings, options.max_world_rules);
    json worldRuleArray = json::array();
    for (const auto* rule : worldRules) {
        worldRuleArray.push_back(filterObject(*rule, options.include_metadata, {"id", "name"}));
    }
    payload["world_rules"] = worldRuleArray;

    // 5k. 章节已有正文（可选）
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
// buildLightweight — 轻量级上下文构建（仅元数据，不注入详细内容）
// ============================================================
std::optional<PromptContext> PromptContextBuilder::buildLightweight(
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

    // 查找章节所属的卷
    const Volume* volume = nullptr;
    if (!chapter->volume_id.empty()) {
        volume = findById(project.outline.volumes, chapter->volume_id);
    }

    json payload = json::object();
    payload["task"] = options.task;
    payload["chapter_id"] = chapter->id;

    // 只保留最核心的信息：项目概要 + 风格 + 章节元数据 + 卷
    if (options.include_project_summary) {
        payload["project"] = filterObject(project, options.include_metadata, {"title"});
    }
    if (options.include_style) {
        payload["style"] = filterObject(project.style, options.include_metadata);
    }
    if (volume) {
        payload["volume"] = filterObject(*volume, options.include_metadata, {"id", "title"});
    }

    // 章节核心字段（放开全部字段，LLM 需要知道本章的基本信息）
    payload["chapter"] = filterObject(*chapter, options.include_metadata, {"id", "title"});

    context.payload = payload;
    context.rendered_prompt = renderPrompt(context);
    return context;
}

// ============================================================
// renderToolUseInstructions
// ============================================================
std::string PromptContextBuilder::renderToolUseInstructions() {
    return R"(
【按需获取上下文指南】
以下是你可用的上下文获取工具，请在需要时随时调用，无需一次获取全部：

- get_latest_chapter() → 获取当前最新章节信息（开始写作前调用了解进度）
- get_chapter_context(chapter_id) → 获取本章核心上下文（剧情线、卷信息）
- get_relevant_characters(chapter_id, max_count) → 获取本章最相关的角色详情
- get_relevant_settings(chapter_id, max_count) → 获取本章最相关的设定/地点
- get_relevant_world_rules(chapter_id, max_count) → 获取本章最相关的世界观规则
- get_character(character_id) → 查询单个角色完整档案（当需要角色深度信息时）
- get_setting(setting_id) → 查询单个设定完整信息
- get_world_rule(rule_id) → 查询单条规则完整信息
- get_outline() → 查看完整大纲视图
- read_style() → 查看完整风格指南
- read_chapter(chapter_id) → 阅读章节全文（确认上下文或修改前阅读）
- search_memory(query) → 语义搜索已写内容

写作流程建议：
1. 先调用 get_latest_chapter() 确认当前写作进度
2. 调用 get_chapter_context() 了解本章要写什么
2. 调用 get_relevant_characters() 了解本章涉及的角色
3. 调用 get_relevant_settings() / get_relevant_world_rules() 了解场景设定
4. 如有需要，用 get_character() / get_setting() 获取单个实体详情
5. 写入完成后用 write_chapter() 保存内容)";
}

// ============================================================
// renderPrompt — 将 JSON payload 渲染为纯文本 Markdown
// ============================================================
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
            if (it.key() == "chapters" || it.key() == "plot_threads") continue;
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
            if (it.key() == "scenes") continue;
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

    const json& charList = payload.value("characters", json::array());
    if (!charList.empty()) {
        oss << "## Characters\n";
        for (const auto& character : charList) {
            const std::string title = character.value("name", character.value("id", "Unnamed"));
            oss << "- " << title << "\n";
            for (auto it = character.begin(); it != character.end(); ++it) {
                if (it.key() == "id" || it.key() == "name" || it.key() == "development") continue;
                if (!isMeaningfulValue(it.value())) continue;
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
