#pragma once

/// 工具执行管线 — Fix #1: 依赖 IToolProvider& 而非 ToolRegistry&。
/// SubAgent 可通过 RestrictedToolProvider 安全调用。

#include "agent/IToolProvider.h"
#include "llm/Conversation.h"
#include "llm/Message.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class ToolPipeline {
public:
    static constexpr size_t kMaxResultChars = 65536;   ///< 工具结果总字符上限（64KB）
    static constexpr size_t kMaxContentChars = 56000;  ///< content 字段字符上限（A15：JSON 对象层面截断，确保 LLM 拿到合法 JSON）

    /// @param tools     工具提供者（ToolRegistry 或 RestrictedToolProvider）
    /// @param conv      对话历史（结果会追加到此）
    ToolPipeline(IToolProvider& tools, llm::Conversation& conv)
        : tools_(tools), conversation_(conv) {}

    void executeAndAppend(const std::vector<llm::ToolCall>& tool_calls);

private:
    IToolProvider& tools_;
    llm::Conversation& conversation_;

    // C9: 工具定义按名缓存——避免每个 tool_call 都全量 getDefinitions() 拷贝 + 线性查找。
    // 单次会话内工具集稳定（SubAgent 的 RestrictedToolProvider 创建后不变），缓存安全。
    // 惰性填充：首次遇到某工具名时从 getDefinitions() 构建并缓存其 parameters schema。
    std::unordered_map<std::string, nlohmann::json> schema_cache_;
    bool cache_populated_ = false;

    std::string executeOne(const llm::ToolCall& tc);
    static std::string truncateResult(std::string result, size_t maxChars);
};

} // namespace agent
