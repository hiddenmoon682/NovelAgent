#include "agent/tools/WorldRuleTools.h"
#include "project/ProjectIO.h"
#include "utils/SchemaUtils.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

namespace {
WorldRule* findRule(std::vector<WorldRule>& r, const std::string& id) {
    auto it = std::find_if(r.begin(), r.end(), [&](const WorldRule& x) { return x.id == id; });
    return (it != r.end()) ? &(*it) : nullptr;
}
} // namespace

json GetWorldRuleTool::parameters() const {
    return utils::schema::object({{"rule_id", utils::schema::stringProp("世界规则 ID")}}, {"rule_id"});
}
json GetWorldRuleTool::execute(const json& args) {
    auto* r = findRule(project_.world_rules, args.value("rule_id", ""));
    if (!r) return {{"error", "世界规则不存在"}};
    spdlog::info("[get_world_rule] {}", r->id);
    return json(*r);
}

json ListWorldRulesTool::parameters() const {
    return utils::schema::object({});
}
json ListWorldRulesTool::execute(const json&) {
    json arr = json::array();
    for (const auto& r : project_.world_rules)
        arr.push_back({{"id", r.id}, {"name", r.name}, {"summary", r.summary}});
    spdlog::info("[get_world_rules] 共 {} 条", arr.size());
    return {{"world_rules", std::move(arr)}};
}

json UpdateWorldRuleTool::parameters() const {
    return utils::schema::object({
        {"rule_id", utils::schema::stringProp("世界规则 ID")},
        {"fields", utils::schema::object({}, {})}
    }, {"rule_id", "fields"});
}
json UpdateWorldRuleTool::execute(const json& args) {
    auto* r = findRule(project_.world_rules, args.value("rule_id", ""));
    if (!r) return {{"error", "世界规则不存在"}};
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};
    int n = 0;
    if (f.contains("name") && f["name"].is_string()) { r->name = f["name"]; n++; }
    if (f.contains("summary") && f["summary"].is_string()) { r->summary = f["summary"]; n++; }
    if (f.contains("limitations") && f["limitations"].is_string()) { r->limitations = f["limitations"]; n++; }
    if (f.contains("costs") && f["costs"].is_string()) { r->costs = f["costs"]; n++; }
    if (f.contains("exceptions") && f["exceptions"].is_string()) { r->exceptions = f["exceptions"]; n++; }
    if (f.contains("known_by") && f["known_by"].is_string()) { r->known_by = f["known_by"]; n++; }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(project_);
    spdlog::info("[update_world_rule] {} 更新 {} 个字段", r->id, n);
    return {{"success", true}, {"rule_id", r->id}, {"updated_fields", n}};
}

} // namespace agent

REGISTER_TOOL(agent::GetWorldRuleTool, "get_world_rule", get_world_rule)
REGISTER_TOOL(agent::ListWorldRulesTool, "get_world_rules", get_world_rules)
REGISTER_TOOL(agent::UpdateWorldRuleTool, "update_world_rule", update_world_rule)
