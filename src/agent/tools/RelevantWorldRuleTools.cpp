/// RelevantWorldRuleTools 实现 — 按章节获取关联世界观规则。

#include "agent/tools/RelevantWorldRuleTools.h"
#include "agent/prompt/PromptSelector.h"
#include "project/Models.h"
#include "project/ProjectAccess.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

json GetRelevantWorldRulesTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID，例如 ch-003")},
        {"max_count", utils::schema::integerProp("最多返回几条规则，默认 6，范围 1-20")}
    }, {"chapter_id"});
}

json GetRelevantWorldRulesTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    if (chapter_id.empty()) {
        return {{"error", "chapter_id 不能为空"}};
    }

    int max_count = args.value("max_count", 6);
    if (max_count < 1) max_count = 1;
    if (max_count > 20) max_count = 20;

    // selector 返回指向 project 内部对象的指针，必须在锁内完成查询与构造
    return project_->withReadLock([&](const Project& p) -> json {
        const auto* chapter = prompt::selector::findById(
            p.outline.chapters, chapter_id);
        if (!chapter) {
            return {{"error", "章节 '" + chapter_id + "' 不存在"}};
        }

        // 先获取关联设定（selectWorldRules 需要它们）
        auto plotThreads = prompt::selector::selectPlotThreads(
            p, *chapter, 10);
        auto settings = prompt::selector::selectSettings(
            p, *chapter, plotThreads, 8);

        auto rules = prompt::selector::selectWorldRules(
            p, settings,
            static_cast<std::size_t>(max_count));

        json ruleArray = json::array();
        for (const auto* rule : rules) {
            ruleArray.push_back(prompt::selector::filterObject(
                *rule, false, {"id", "name"}));
        }

        spdlog::info("[get_relevant_world_rules] chapter={}, count={}",
                     chapter_id, rules.size());
        return {
            {"chapter_id", chapter_id},
            {"total_count", rules.size()},
            {"world_rules", ruleArray}
        };
    });
}

} // namespace agent

REGISTER_TOOL(agent::GetRelevantWorldRulesTool, "get_relevant_world_rules", get_relevant_world_rules)
