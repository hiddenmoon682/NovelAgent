/// ToolPipeline 实现 — Fix #1: 依赖 IToolProvider&。

#include "agent/ToolPipeline.h"
#include "agent/ParameterValidator.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace agent {

void ToolPipeline::executeAndAppend(const std::vector<llm::ToolCall>& tool_calls)
{
    for (const auto& tc : tool_calls) {
        spdlog::info("[ToolPipeline] 执行: {} (id={})", tc.function_name, tc.id);
        std::string result = executeOne(tc);
        conversation_.addToolResult(tc.id, std::move(result));
    }
}

std::string ToolPipeline::executeOne(const llm::ToolCall& tc)
{
    nlohmann::json args;
    if (!tc.arguments.empty()) {
        try {
            args = nlohmann::json::parse(tc.arguments);
        } catch (const nlohmann::json::parse_error& e) {
            return nlohmann::json{
                {"error", "参数 JSON 解析失败"},
                {"detail", e.what()},
                {"retryable", false},
                {"suggestion", "请检查参数格式后重试"}
            }.dump();
        }
    }

    // 参数 Schema 校验（通过 IToolProvider 获取工具定义）
    auto tool_defs = tools_.getDefinitions();
    for (const auto& def : tool_defs) {
        if (def.name == tc.function_name) {
            auto validation = ParameterValidator::validate(def.parameters, args);
            if (!validation.valid) {
                nlohmann::json err = validation.toJson();
                err["retryable"] = true;
                err["suggestion"] = "请根据 details 中的提示修正参数后重试";
                return err.dump();
            }
            break;
        }
    }

    // 通过 IToolProvider 执行（支持 RestrictedToolProvider 安全约束）
    auto result = tools_.execute(tc.function_name, args);

    std::string result_str = result.dump();
    if (result_str.size() > kMaxResultChars) {
        size_t cut = kMaxResultChars;
        while (cut > kMaxResultChars / 2) {
            if (result_str[cut] == ',' || result_str[cut] == '}' || result_str[cut] == ']') break;
            --cut;
        }
        nlohmann::json truncated;
        truncated["partial"] = true;
        truncated["truncated"] = true;
        truncated["original_size"] = result_str.size();
        truncated["preview"] = result_str.substr(0, cut + 1);
        truncated["note"] = "结果过大已截断（原始 " + std::to_string(result_str.size()) + " 字符）";
        return truncated.dump();
    }
    return result_str;
}

std::string ToolPipeline::truncateResult(std::string result, size_t maxChars)
{
    if (result.size() <= maxChars) return result;
    return result.substr(0, maxChars)
         + "\n...(已截断，共 " + std::to_string(result.size()) + " 字符)";
}

} // namespace agent
