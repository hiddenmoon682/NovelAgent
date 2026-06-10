#pragma once

/// 工具执行管线 — 声明与实现分离，避免头文件引入 spdlog。
/// 实现移至 ToolPipeline.cpp。

#include "agent/ToolRegistry.h"
#include "llm/Conversation.h"
#include "llm/Message.h"

#include <string>
#include <vector>

namespace agent {

class ToolPipeline {
public:
    static constexpr size_t kMaxResultChars = 32000;

    ToolPipeline(ToolRegistry& registry, llm::Conversation& conv)
        : registry_(registry), conversation_(conv) {}

    void executeAndAppend(const std::vector<llm::ToolCall>& tool_calls);

private:
    ToolRegistry& registry_;
    llm::Conversation& conversation_;

    std::string executeOne(const llm::ToolCall& tc);
    static std::string truncateResult(std::string result, size_t maxChars);
};

} // namespace agent
