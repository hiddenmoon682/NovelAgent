/// RelevantCharacterTools 实现 — 按章节获取关联角色。

#include "agent/tools/RelevantCharacterTools.h"
#include "agent/PromptSelector.h"
#include "project/Models.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

json GetRelevantCharactersTool::parameters() const {
    return utils::schema::object({
        {"chapter_id", utils::schema::stringProp("章节 ID，例如 ch-003")},
        {"max_count", utils::schema::integerProp("最多返回几个角色，默认 8，范围 1-20")}
    }, {"chapter_id"});
}

json GetRelevantCharactersTool::execute(const json& args) {
    std::string chapter_id = args.value("chapter_id", "");
    if (chapter_id.empty()) {
        return {{"error", "chapter_id 不能为空"}};
    }

    const auto* chapter = prompt::selector::findById(
        project_->outline.chapters, chapter_id);
    if (!chapter) {
        return {{"error", "章节 '" + chapter_id + "' 不存在"}};
    }

    int max_count = args.value("max_count", 8);
    if (max_count < 1) max_count = 1;
    if (max_count > 20) max_count = 20;

    // 先获取关联剧情线（selectCharacters 需要它们）
    auto plotThreads = prompt::selector::selectPlotThreads(
        *project_, *chapter, 10);

    auto characters = prompt::selector::selectCharacters(
        *project_, *chapter, plotThreads,
        static_cast<std::size_t>(max_count));

    json charArray = json::array();
    for (const auto* ch : characters) {
        charArray.push_back(prompt::selector::filterObject(
            *ch, false, {"id", "name"}));
    }

    spdlog::info("[get_relevant_characters] chapter={}, count={}",
                 chapter_id, characters.size());
    return {
        {"chapter_id", chapter_id},
        {"total_count", characters.size()},
        {"characters", charArray}
    };
}

} // namespace agent

REGISTER_TOOL(agent::GetRelevantCharactersTool, "get_relevant_characters", get_relevant_characters)
