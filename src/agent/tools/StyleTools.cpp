/// ReadStyleTool 实现 — 允许 LLM 主动查询完整写作风格配置。

#include "agent/tools/StyleTools.h"

#include "project/Models.h"   // Style 的 to_json 序列化
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

} // namespace agent

REGISTER_TOOL(agent::ReadStyleTool, "read_style", read_style)
