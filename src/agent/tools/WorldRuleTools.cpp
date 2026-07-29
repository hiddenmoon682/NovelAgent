#include "agent/tools/WorldRuleTools.h"
#include "project/ProjectIO.h"
#include "utils/IdUtils.h"
#include "utils/SchemaUtils.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

namespace {
WorldRule* findRule(std::vector<WorldRule>& r, const std::string& id) {
    return utils::id::findById(r, id);
}

// A6: 软校验——warn 不阻断
template<typename T>
static void validateIdArray(const T& container, const std::vector<std::string>& ids, const std::string& field, const std::string& caller) {
    for (const auto& id : ids) {
        if (id.empty()) continue;
        auto it = std::find_if(container.begin(), container.end(), [&](const auto& e) { return e.id == id; });
        if (it == container.end()) spdlog::warn("[{}] {} 引用的 ID {} 不存在", caller, field, id);
    }
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
    // C5: fields 列出全部可更新字段（A16 新增 contradicts_with/precedence 一并列出）
    return utils::schema::object({
        {"rule_id", utils::schema::stringProp("世界规则 ID")},
        {"fields", utils::schema::object({
            {"name",             utils::schema::stringProp("规则名称")},
            {"summary",          utils::schema::stringProp("规则概要")},
            {"limitations",      utils::schema::stringProp("限制")},
            {"costs",            utils::schema::stringProp("代价")},
            {"exceptions",       utils::schema::stringProp("例外")},
            {"known_by",         utils::schema::stringProp("知晓范围")},
            {"precedence",       utils::schema::integerProp("优先级（数字越大越优先，冲突时高优先级规则胜出）")},
            {"contradicts_with", utils::schema::stringArrayProp("与本规则冲突的世界规则 ID 列表")},
            {"related_settings", utils::schema::stringArrayProp("关联设定 ID 列表")},
        }, {}, /*allowExtra=*/false)}
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
        {"contradicts_with", &WorldRule::contradicts_with},  // A16: 冲突规则声明
    };

    int n = 0;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const std::string& key = it.key();
        if (auto si = kStringMap.find(key); si != kStringMap.end() && it.value().is_string()) {
            r->*si->second = it.value().get<std::string>();
            n++;
        } else if (key == "precedence" && it.value().is_number_integer()) {
            // A16: 优先级（整数）
            r->precedence = it.value().get<int>();
            n++;
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && it.value().is_array()) {
            auto& arr = r->*ai->second;
            arr.clear();
            for (const auto& v : it.value()) arr.push_back(v.get<std::string>());
            if (key == "related_settings") validateIdArray(project_->settings, arr, "related_settings", "update_world_rule");
            // A16: contradicts_with 引用的是其它世界规则 ID，软校验存在性
            if (key == "contradicts_with") validateIdArray(project_->world_rules, arr, "contradicts_with", "update_world_rule");
            n++;
        }
    }
    if (n == 0) return {{"error", "没有可更新的字段"}};
    project_->markDirty(Project::DIRTY_WORLD_RULES);
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
        // D2: 非标准 ID 解析失败时安全跳过，不参与编号统计
        if (auto num = utils::id::tryParseIdNumber(r.id, "rule-")) {
            max_num = std::max(max_num, *num);
        }
    }

    WorldRule new_r;
    new_r.id = utils::id::formatSequentialId("rule-", max_num + 1);

    new_r.name        = name;
    new_r.summary     = args.value("summary", "");
    new_r.limitations = args.value("limitations", "");
    new_r.costs       = args.value("costs", "");
    new_r.exceptions  = args.value("exceptions", "");
    new_r.known_by    = args.value("known_by", "");

    project_->world_rules.push_back(new_r);
    project_->markDirty(Project::DIRTY_WORLD_RULES);
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

// ===========================================================================
// DeleteWorldRuleTool
// ===========================================================================

json DeleteWorldRuleTool::parameters() const {
    return utils::schema::object({
        {"rule_id", utils::schema::stringProp("要删除的世界规则 ID")}
    }, {"rule_id"});
}

json DeleteWorldRuleTool::execute(const json& args) {
    const std::string rid = args.value("rule_id", "");
    if (rid.empty()) return {{"error", "rule_id 不能为空"}};

    // 1) 从 world_rules 中移除
    auto it = std::find_if(project_->world_rules.begin(), project_->world_rules.end(),
        [&](const WorldRule& r) { return r.id == rid; });
    if (it == project_->world_rules.end()) return {{"error", "世界规则不存在: " + rid}};
    project_->world_rules.erase(it);

    // 2) 级联清理：所有 Setting.related_rule_ids 中移除该 ID
    int cascade_settings = 0;
    for (auto& s : project_->settings) {
        auto& rr = s.related_rule_ids;
        auto before = rr.size();
        rr.erase(std::remove(rr.begin(), rr.end(), rid), rr.end());
        if (rr.size() < before) cascade_settings += static_cast<int>(before - rr.size());
    }

    project_->markDirty(Project::DIRTY_WORLD_RULES);
    ProjectIO::save(*project_);
    spdlog::info("[delete_world_rule] {} 已删除 (cascade: settings={})", rid, cascade_settings);
    return {
        {"success", true},
        {"deleted_id", rid},
        {"cascade", {
            {"settings_cleaned", cascade_settings}
        }}
    };
}

} // namespace agent
REGISTER_TOOL(agent::GetWorldRuleTool, "get_world_rule", get_world_rule)
REGISTER_TOOL(agent::ListWorldRulesTool, "get_world_rules", get_world_rules)
REGISTER_TOOL(agent::UpdateWorldRuleTool, "update_world_rule", update_world_rule)
REGISTER_TOOL(agent::CreateWorldRuleTool, "create_world_rule", create_world_rule)
REGISTER_TOOL(agent::DeleteWorldRuleTool, "delete_world_rule", delete_world_rule)
