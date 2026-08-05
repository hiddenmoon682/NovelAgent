#pragma once

// 工具执行管线 — Fix #1: 依赖 IToolProvider& 而非 ToolRegistry&。
// SubAgent 可通过 RestrictedToolProvider 安全调用。
// 并发执行：只读工具通过内部 ThreadPool 并发，写工具串行，结果按原序返回。

#include "agent/tool/IToolProvider.h"
#include "agent/tool/ThreadPool.h"
#include "agent/context/Memory.h"
#include "llm/Message.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class ToolPipeline {
public:
    // 单条工具结果 JSON 字符串的总长上限，超出时整体截断并追加截断标记。
    static constexpr size_t kMaxResultChars = 131072;
    // 结果中 content 字段的长度上限（UTF-8 字符感知截断，不斩断多字节字符）；
    // 先于 kMaxResultChars 生效，针对读章节等大正文工具。
    static constexpr size_t kMaxContentChars = 22000;

    // 构造工具执行管线。
    // @param tools 工具提供者（ToolRegistry 或 RestrictedToolProvider）；
    //              非拥有引用，调用方保证其存活期覆盖本对象。
    // @param num_threads 并发线程数（每会话 2；D9 不再支持 0=全串行）。
    explicit ToolPipeline(IToolProvider& tools, size_t num_threads = 4)
        : tools_(tools)
        , pool_(std::make_unique<ThreadPool>(num_threads)) {}

    // 执行一批工具调用：只读工具经线程池并发，写工具串行，
    // 结果严格按 tool_calls 原序返回。
    // @param tool_calls LLM 返回的工具调用列表。
    // @return 包含每条工具结果（role=tool）消息的 MemoryDiff，
    //         供调用方 apply 到对话历史；单条失败不中断其余调用，
    //         错误以 {"error": ...} 形式写入对应结果。
    llm::MemoryDiff execute(const std::vector<llm::ToolCall>& tool_calls);

private:
    IToolProvider& tools_;
    std::unique_ptr<ThreadPool> pool_;

    std::unordered_map<std::string, nlohmann::json> schema_cache_;

    std::string executeOne(const llm::ToolCall& tc);
};

} // namespace agent
