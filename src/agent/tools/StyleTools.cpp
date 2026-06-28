/// ReadStyleTool 实现 — 允许 LLM 主动查询完整写作风格配置。

#include "agent/tools/StyleTools.h"

#include "project/Models.h"   // Style 的 to_json 序列化
#include "project/ProjectIO.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

// ===========================================================================
// parameters — 无参数，返回完整 Style
// ===========================================================================

json ReadStyleTool::parameters() const {
    return utils::schema::object({});
}

// ===========================================================================
// execute — 序列化并返回完整 Style 对象
//
// 利用 Style.h 中已有的 to_json(nlohmann::json&, const Style&) 重载
// 生成基础 JSON，再附加人类可读的摘要字段帮助 LLM 快速理解配置。
// ===========================================================================

json ReadStyleTool::execute(const json& /*args*/) {
    const auto& s = project_->style;
    spdlog::info("[read_style] tone={}, pov={}, prose={}", s.tone, s.pov, s.prose_style);

    // 基础序列化（复用 Style.h 的 to_json）
    json j = s;

    // 附加便于 LLM 阅读的摘要字段
    if (j.contains("generation") && j["generation"].is_object()) {
        j["generation_enabled"] = s.generation.enabled;
        if (!s.generation.prompt_hint.empty()) {
            j["generation_prompt_hint"] = s.generation.prompt_hint;
        }
    }

    // 禁用短语清单 → 中文顿号拼接
    if (!s.forbidden_phrases.empty()) {
        std::string summary;
        for (size_t i = 0; i < s.forbidden_phrases.size(); ++i) {
            if (i > 0) summary += "、";
            summary += s.forbidden_phrases[i];
        }
        j["forbidden_phrases_summary"] = summary;
    }

    // 禁用套路清单 → 中文顿号拼接
    if (!s.forbidden_tropes.empty()) {
        std::string summary;
        for (size_t i = 0; i < s.forbidden_tropes.size(); ++i) {
            if (i > 0) summary += "、";
            summary += s.forbidden_tropes[i];
        }
        j["forbidden_tropes_summary"] = summary;
    }

    return j;
}

// ===========================================================================
// UpdateStyleTool — 更新写作风格配置
//
// Style 是项目级单例，无需 id。使用字段指针白名单模式安全写入。
// ===========================================================================

json UpdateStyleTool::parameters() const {
    return utils::schema::object({
        {"fields", utils::schema::object({}, {})}
    }, {"fields"});
}

json UpdateStyleTool::execute(const json& args) {
    const json& f = args["fields"];
    if (!f.is_object() || f.empty()) return {{"error", "fields 必须是非空对象"}};

    auto& s = project_->style;

    // 字符串字段白名单
    using StrField = std::string Style::*;
    static const std::map<std::string, StrField> kStringMap = {
        {"tone", &Style::tone},
        {"pacing", &Style::pacing},
        {"pov", &Style::pov},
        {"tense", &Style::tense},
        {"prose_style", &Style::prose_style},
        {"dialogue_style", &Style::dialogue_style},
        {"narrative_distance", &Style::narrative_distance},
        {"sentence_length", &Style::sentence_length},
        {"vocabulary", &Style::vocabulary},
        {"voice_reference", &Style::voice_reference},
        {"show_vs_tell_bias", &Style::show_vs_tell_bias},
        {"dialogue_density", &Style::dialogue_density},
        {"description_density", &Style::description_density},
        {"introspection_density", &Style::introspection_density},
        {"humor_level", &Style::humor_level},
        {"sensory_focus", &Style::sensory_focus},
        {"chapter_opening_style", &Style::chapter_opening_style},
        {"chapter_ending_style", &Style::chapter_ending_style},
        {"notes", &Style::notes},
    };

    // 整数字段白名单（ptr-to-member，与字符串字段模式统一）
    using IntField = int Style::*;
    static const std::map<std::string, IntField> kIntMap = {
        {"chapter_length_target", &Style::chapter_length_target},
    };

    // 字符串数组字段白名单
    using ArrField = std::vector<std::string> Style::*;
    static const std::map<std::string, ArrField> kArrayMap = {
        {"forbidden_phrases", &Style::forbidden_phrases},
        {"forbidden_tropes", &Style::forbidden_tropes},
        {"tags", &Style::tags},
    };

    std::vector<std::string> updated;
    for (auto it = f.begin(); it != f.end(); ++it) {
        const std::string& key = it.key();
        const json& value = it.value();

        if (auto si = kStringMap.find(key); si != kStringMap.end() && value.is_string()) {
            s.*si->second = value.get<std::string>();
            updated.push_back(key);
        } else if (auto ii = kIntMap.find(key); ii != kIntMap.end() && value.is_number_integer()) {
            s.*ii->second = value.get<int>();
            updated.push_back(key);
        } else if (auto ai = kArrayMap.find(key); ai != kArrayMap.end() && value.is_array()) {
            auto& arr = s.*ai->second;
            arr.clear();
            for (const auto& v : value) arr.push_back(v.get<std::string>());
            updated.push_back(key);
        }
    }

    if (updated.empty()) {
        return {{"error", "没有可以更新的字段。请检查字段名是否在白名单中，以及值的类型是否匹配。"}};
    }

    ProjectIO::save(*project_);
    std::string fields_str;
    for (size_t i = 0; i < updated.size(); ++i) {
        if (i > 0) fields_str += ", ";
        fields_str += updated[i];
    }
    spdlog::info("[update_style] 更新 {} 个字段: {}", updated.size(), fields_str);

    return {{"success", true}, {"updated_fields", updated}};
}

} // namespace agent

REGISTER_TOOL(agent::ReadStyleTool, "read_style", read_style)
REGISTER_TOOL(agent::UpdateStyleTool, "update_style", update_style)
