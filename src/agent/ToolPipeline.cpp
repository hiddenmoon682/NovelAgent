#include "agent/ToolPipeline.h"

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
