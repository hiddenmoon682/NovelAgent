#pragma once

// 工具执行管线 — Fix #1: 依赖 IToolProvider& 而非 ToolRegistry&。
// SubAgent 可通过 RestrictedToolProvider 安全调用。
// 并发执行：只读工具通过内部 ThreadPool 并发，写工具串行，结果按原序返回。

#include "agent/tool/IToolProvider.h"
#include "agent/tool/ThreadPool.h"
#include "agent/context/Memory.h"
#include "llm/Message.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class ToolPipeline {
public:
    static constexpr size_t kMaxResultChars = 131072;
    static constexpr size_t kMaxContentChars = 22000;

    // tools        工具提供者（ToolRegistry 或 RestrictedToolProvider）
    // num_threads  并发线程数（0 = 全部串行执行）
    explicit ToolPipeline(IToolProvider& tools, size_t num_threads = 4)
        : tools_(tools)
        , pool_(num_threads > 0 ? std::make_unique<ThreadPool>(num_threads) : nullptr) {}

    llm::MemoryDiff execute(const std::vector<llm::ToolCall>& tool_calls);

private:
    IToolProvider& tools_;
    std::unique_ptr<ThreadPool> pool_;

    std::unordered_map<std::string, nlohmann::json> schema_cache_;

    std::string executeOne(const llm::ToolCall& tc);
    static bool isReadOnly(const std::string& tool_name);
};

} // namespace agent
