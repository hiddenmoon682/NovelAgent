/// RelevantSettingTools 实现 — 按章节获取关联设定/地点。

#include "agent/tools/RelevantSettingTools.h"
#include "agent/prompt/PromptSelector.h"
#include "project/Models.h"
#include "project/ProjectAccess.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

json GetRelevantSettingsTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID，例如 ch-003")},
        {"max_count", utils::schema::integerProp("最多返回几个设定，默认 8，范围 1-20")}
    }, {"chapter_id"});
}

json GetRelevantSettingsTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    if (chapter_id.empty()) {
        return {{"error", "chapter_id 不能为空"}};
    }

    int max_count = args.value("max_count", 8);
    if (max_count < 1) max_count = 1;
    if (max_count > 20) max_count = 20;

    // selector 返回指向 project 内部对象的指针，必须在锁内完成查询与构造
    return project_->withReadLock([&](const Project& p) -> json {
        const auto* chapter = prompt::selector::findById(
            p.outline.chapters, chapter_id);
        if (!chapter) {
            return {{"error", "章节 '" + chapter_id + "' 不存在"}};
        }

        auto plotThreads = prompt::selector::selectPlotThreads(
            p, *chapter, 10);

        auto settings = prompt::selector::selectSettings(
            p, *chapter, plotThreads,
            static_cast<std::size_t>(max_count));

        json settingArray = json::array();
        for (const auto* s : settings) {
            settingArray.push_back(prompt::selector::filterObject(
                *s, false, {"id", "name", "category"}));
        }

        spdlog::info("[get_relevant_settings] chapter={}, count={}",
                     chapter_id, settings.size());
        return {
            {"chapter_id", chapter_id},
            {"total_count", settings.size()},
            {"settings", settingArray}
        };
    });
}

} // namespace agent

REGISTER_TOOL(agent::GetRelevantSettingsTool, "get_relevant_settings", get_relevant_settings)
