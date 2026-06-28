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
}

json GetWorldRuleTool::parameters() const {
    return utils::schema::object({{"rule_id", utils::schema::stringProp("世界规则 ID")}}, {"rule_id"});
}
json GetWorldRuleTool::execute(const json& args) {
    auto* r = findRule(project_->world_rules, args.value("rule_id", ""));
    if (!r) return {{"error", "世界规则不存在"}};
    spdlog::info("[get_world_rule] {}", r->id);
    return json(*r);
}

json ListWorldRulesTool::parameters() const {
    return utils::schema::object({});
}
json ListWorldRulesTool::execute(const json&) {
    json arr = json::array();
    for (const auto& r : project_->world_rules)
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
    auto* r = findRule(project_->world_rules, args.value("rule_id", ""));
    if (!r) return {{"error", "世界规则不存在"}};
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};

    // 字符串字段白名单
    using StrField = std::string WorldRule::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"name", &WorldRule::name},
        {"summary", &WorldRule::summary},
        {"limitations", &WorldRule::limitations},
        {"costs", &WorldRule::costs},
        {"exceptions", &WorldRule::exceptions},
        {"known_by", &WorldRule::known_by},
    };

    // 字符串数组字段白名单
    using ArrField = std::vector<std::string> WorldRule::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"related_settings", &WorldRule::related_settings},
        {"tags", &WorldRule::tags},
    };

    int n = 0;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const std::string& key = it.key();
        if (auto si = kStringMap.find(key); si != kStringMap.end() && it.value().is_string()) {
            r->*si->second = it.value().get<std::string>();
            n++;
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && it.value().is_array()) {
            auto& arr = r->*ai->second;
            arr.clear();
            for (const auto& v : it.value()) arr.push_back(v.get<std::string>());
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    ProjectIO::save(*project_);
    spdlog::info("[update_world_rule] {} 更新 {} 个字段", r->id, n);
    return {{"success", true}, {"rule_id", r->id}, {"updated_fields", n}};
}

// ===========================================================================
// CreateWorldRuleTool
// ===========================================================================

json CreateWorldRuleTool::parameters() const {
    return utils::schema::object({
        {"name", utils::schema::stringProp("规则名称，例如 '宵禁法'")},
        {"summary", utils::schema::stringProp("规则概要（可选）")},
        {"limitations", utils::schema::stringProp("限制条件（可选）")},
        {"costs", utils::schema::stringProp("使用代价/惩罚（可选）")},
        {"exceptions", utils::schema::stringProp("例外情况（可选）")},
        {"known_by", utils::schema::stringProp("知晓者范围（可选）")}
    }, {"name"});
}

json CreateWorldRuleTool::execute(const json& args) {
    std::string name = args.value("name", "");
    if (name.empty()) return {{"error", "规则名称不能为空"}};

    // 生成 ID: rule-001 格式
    int max_num = 0;
    for (const auto& r : project_->world_rules) {
        if (r.id.size() >= 6 && r.id.substr(0, 5) == "rule-") {
            try { max_num = std::max(max_num, std::stoi(r.id.substr(5))); }
            catch (...) {}
        }
    }

    WorldRule new_r;
    if (max_num + 1 < 10)       new_r.id = "rule-00" + std::to_string(max_num + 1);
    else if (max_num + 1 < 100) new_r.id = "rule-0" + std::to_string(max_num + 1);
    else                         new_r.id = "rule-" + std::to_string(max_num + 1);

    new_r.name        = name;
    new_r.summary     = args.value("summary", "");
    new_r.limitations = args.value("limitations", "");
    new_r.costs       = args.value("costs", "");
    new_r.exceptions  = args.value("exceptions", "");
    new_r.known_by    = args.value("known_by", "");

    project_->world_rules.push_back(new_r);
    ProjectIO::save(*project_);

    spdlog::info("[create_world_rule] {} '{}'", new_r.id, name);

    return {
        {"success", true},
        {"rule", {
            {"id", new_r.id},
            {"name", new_r.name}
        }}
    };
}

} // namespace agent
REGISTER_TOOL(agent::GetWorldRuleTool, "get_world_rule", get_world_rule)
REGISTER_TOOL(agent::ListWorldRulesTool, "get_world_rules", get_world_rules)
REGISTER_TOOL(agent::UpdateWorldRuleTool, "update_world_rule", update_world_rule)
REGISTER_TOOL(agent::CreateWorldRuleTool, "create_world_rule", create_world_rule)
