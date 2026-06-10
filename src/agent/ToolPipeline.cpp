#include "agent/ToolPipeline.h"
#include "agent/ParameterValidator.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace agent {

void ToolPipeline::executeAndAppend(
    const std::vector<llm::ToolCall>& tool_calls)
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
            spdlog::error("[ToolPipeline] JSON 解析失败: {} — args='{}'",
                          e.what(), tc.arguments);
            nlohmann::json err = {
                {"error", std::string("参数 JSON 解析失败: ") + e.what()}
            };
            return err.dump();
        }
    }

    // Phase 5.5: 参数 Schema 校验
    auto tool_defs = registry_.getToolDefinitions();
    for (const auto& def : tool_defs) {
        if (def.name == tc.function_name) {
            auto validation = ParameterValidator::validate(def.parameters, args);
            if (!validation.valid) {
                spdlog::warn("[ToolPipeline] 参数校验失败: {}", tc.function_name);
                return validation.toJson().dump();
            }
            break;
        }
    }

    auto result = registry_.executeTool(tc.function_name, args);
    return truncateResult(result.dump(), kMaxResultChars);
}

std::string ToolPipeline::truncateResult(std::string result, size_t maxChars)
{
    if (result.size() <= maxChars) return result;
    return result.substr(0, maxChars)
         + "\n...(已截断，共 " + std::to_string(result.size()) + " 字符)";
}

} // namespace agent
