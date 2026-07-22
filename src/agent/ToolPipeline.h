#pragma once

// 工具执行管线 — Fix #1: 依赖 IToolProvider& 而非 ToolRegistry&。
// SubAgent 可通过 RestrictedToolProvider 安全调用。
// 并发执行：只读工具通过 ThreadPool 并发，写工具串行，结果按原序返回。

#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/Message.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class ThreadPool;

class ToolPipeline {
public:
    static constexpr size_t kMaxResultChars = 131072;
    static constexpr size_t kMaxContentChars = 22000;

    // tools     工具提供者（ToolRegistry 或 RestrictedToolProvider）
    // pool      可选线程池（nullptr = 全部串行执行）
    explicit ToolPipeline(IToolProvider& tools, ThreadPool* pool = nullptr)
        : tools_(tools), pool_(pool) {}

    llm::ConversationDiff execute(const std::vector<llm::ToolCall>& tool_calls);

private:
    IToolProvider& tools_;
    ThreadPool* pool_;

    std::unordered_map<std::string, nlohmann::json> schema_cache_;
    bool cache_populated_ = false;

    std::string executeOne(const llm::ToolCall& tc);
    static bool isReadOnly(const std::string& tool_name);
};

} // namespace agent
