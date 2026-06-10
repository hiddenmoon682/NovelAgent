#pragma once

/// 工具执行管线 — Fix #1: 依赖 IToolProvider& 而非 ToolRegistry&。
/// SubAgent 可通过 RestrictedToolProvider 安全调用。

#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/Message.h"

#include <string>
#include <vector>

namespace agent {

class ToolPipeline {
public:
    static constexpr size_t kMaxResultChars = 32000;

    /// @param tools     工具提供者（ToolRegistry 或 RestrictedToolProvider）
    /// @param conv      对话历史（结果会追加到此）
    ToolPipeline(IToolProvider& tools, llm::Conversation& conv)
        : tools_(tools), conversation_(conv) {}

    void executeAndAppend(const std::vector<llm::ToolCall>& tool_calls);

private:
    IToolProvider& tools_;
    llm::Conversation& conversation_;

    std::string executeOne(const llm::ToolCall& tc);
    static std::string truncateResult(std::string result, size_t maxChars);
};

} // namespace agent
