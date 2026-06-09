#pragma once

/// 工具执行管线 — 统一参数校验、执行、结果截断和错误处理。
///
/// 替代 Agent::executeToolCallsAndAppend 中的分散逻辑，
/// 为所有工具提供一致的行为：校验 → 执行 → 格式化 → 截断。

#include "agent/ToolRegistry.h"
#include "llm/Conversation.h"
#include "llm/Message.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

namespace agent {

class ToolPipeline {
public:
    /// 最大单条工具结果字符数（安全上限，~8000 中文字）。
    /// 内容类工具（read_chapter）可返回完整章节内容；
    /// 列表类工具（list_chapters）本身已限制输出大小。
    /// 超出上限的结果由 ContextManager 在消息截断阶段处理。
    static constexpr size_t kMaxResultChars = 32000;

    /// @param registry  工具注册中心
    /// @param conv      对话历史（结果将追加到此）
    explicit ToolPipeline(ToolRegistry& registry, llm::Conversation& conv)
        : registry_(registry), conversation_(conv) {}

    /// 执行一批工具调用并将结果加入对话。
    /// 每个 tool_call 独立执行，单个失败不影响其他。
    void executeAndAppend(const std::vector<llm::ToolCall>& tool_calls);

private:
    ToolRegistry& registry_;
    llm::Conversation& conversation_;

    /// 执行单个工具调用并返回格式化结果字符串
    std::string executeOne(const llm::ToolCall& tc);

    /// 截断过长结果
    static std::string truncateResult(std::string result, size_t maxChars);
};

// ── 内联实现 ──

inline void ToolPipeline::executeAndAppend(
    const std::vector<llm::ToolCall>& tool_calls)
{
    for (const auto& tc : tool_calls) {
        spdlog::info("[ToolPipeline] 执行: {} (id={})", tc.function_name, tc.id);
        std::string result = executeOne(tc);
        conversation_.addToolResult(tc.id, std::move(result));
    }
}

inline std::string ToolPipeline::executeOne(const llm::ToolCall& tc)
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

inline std::string ToolPipeline::truncateResult(std::string result, size_t maxChars)
{
    if (result.size() <= maxChars) return result;
    return result.substr(0, maxChars)
         + "\n...(已截断，共 " + std::to_string(result.size()) + " 字符)";
}

} // namespace agent
