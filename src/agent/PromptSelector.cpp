/// PromptSelector 实现 — 关联对象选择逻辑。

#include "agent/PromptSelector.h"

#include <algorithm>
#include <sstream>

namespace prompt {
namespace selector {

std::vector<const PlotThread*> selectPlotThreads(
    const Project& project,
    const Chapter& chapter,
    std::size_t maxCount)
{
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

std::vector<const Character*> selectCharacters(
    const Project& project,
    const Chapter& chapter,
    const std::vector<const PlotThread*>& plotThreads,
    std::size_t maxCount)
{
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

std::vector<const Setting*> selectSettings(
    const Project& project,
    const Chapter& chapter,
    const std::vector<const PlotThread*>& plotThreads,
    std::size_t maxCount)
{
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
    std::size_t maxCount)
{
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

std::string renderSectionList(const nlohmann::json& values, const std::string& heading) {
    if (!values.is_array() || values.empty()) {
        return {};
    }

    std::ostringstream oss;
    oss << "## " << heading << "\n";
    for (const auto& item : values) {
        const std::string title = item.value("name",
            item.value("title", item.value("id", "Unnamed")));
        oss << "- " << title << "\n";
        for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() == "id" || it.key() == "name" || it.key() == "title") continue;
            if (!isMeaningfulValue(it.value())) continue;

            if (it.value().is_string()) {
                oss << "  " << it.key() << ": " << it.value().get<std::string>() << "\n";
            } else if (it.value().is_array()) {
                std::vector<std::string> pieces;
                for (const auto& entry : it.value()) {
                    if (entry.is_string()) pieces.push_back(entry.get<std::string>());
                    else pieces.push_back(entry.dump());
                }
                std::string joined;
                for (size_t i = 0; i < pieces.size(); ++i) {
                    if (i > 0) joined += ", ";
                    joined += pieces[i];
                }
                oss << "  " << it.key() << ": " << joined << "\n";
            } else {
                oss << "  " << it.key() << ": " << it.value().dump() << "\n";
            }
        }
    }
    oss << "\n";
    return oss.str();
}

} // namespace selector
} // namespace prompt
