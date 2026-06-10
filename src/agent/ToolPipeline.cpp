/// ToolPipeline 实现 — Agent 最佳实践增强版 (Fix #1)。

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

    // 参数 Schema 校验
    auto tool_defs = registry_.getToolDefinitions();
    for (const auto& def : tool_defs) {
        if (def.name == tc.function_name) {
            auto validation = ParameterValidator::validate(def.parameters, args);
            if (!validation.valid) {
                // Fix #1: 结构化错误——告知 LLM 具体失败原因和修复建议
                nlohmann::json err = validation.toJson();
                err["retryable"] = true;
                err["suggestion"] = "请根据 details 中的提示修正参数后重试";
                return err.dump();
            }
            break;
        }
    }

    auto result = registry_.executeTool(tc.function_name, args);

    // Fix #1: 语义截断——在 JSON 边界处截断，添加 truncated 标记
    std::string result_str = result.dump();
    if (result_str.size() > kMaxResultChars) {
        // 尝试在最后一个完整 JSON 值处截断
        size_t cut = kMaxResultChars;
        // 往前找到最近的逗号或闭合括号
        while (cut > kMaxResultChars / 2) {
            if (result_str[cut] == ',' || result_str[cut] == '}' || result_str[cut] == ']') {
                break;
            }
            --cut;
        }
        // 构造截断响应
        nlohmann::json truncated;
        truncated["partial"] = true;
        truncated["truncated"] = true;
        truncated["original_size"] = result_str.size();
        truncated["preview"] = result_str.substr(0, cut + 1);
        truncated["note"] = "结果过大已截断（原始 " +
            std::to_string(result_str.size()) + " 字符），如需完整数据请使用更精确的查询参数。";
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
