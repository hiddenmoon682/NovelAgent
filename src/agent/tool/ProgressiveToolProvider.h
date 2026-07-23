#pragma once

#include "agent/tool/IToolProvider.h"
#include "agent/tool/ToolRegistry.h"
#include "llm/Message.h"

#include <set>
#include <shared_mutex>
#include <string>
#include <vector>

namespace agent {

// 渐进式工具提供者 — 参考 Claude Code defer_loading / OpenCode mcp_search 模式。
//
// LLM 初始只看到核心工具（完整 schema）+ tool_search 发现工具，
// 需要其他工具时调用 tool_search 搜索并加载完整 schema，
// 加载后工具出现在后续轮次的 tools 数组中。
//
// 三层强制：
//   1. API 硬约束：未加载工具不在 tools 数组 → API 拒绝直接调用
//   2. System prompt 规则：指示 LLM 先用 tool_search
//   3. 中间件：execute() 拒绝未加载工具并返回引导错误
//
// 线程安全：loaded_tools_ 通过 shared_mutex 保护（ThreadPool 并发读 + tool_search 写）。
class ProgressiveToolProvider : public IToolProvider {
public:
    explicit ProgressiveToolProvider(ToolRegistry& registry);

    // ── IToolProvider 接口 ──
    std::vector<llm::ToolDefinition> getDefinitions() const override;
    nlohmann::json execute(const std::string& name, const nlohmann::json& args) override;
    bool has(const std::string& name) const override;
    std::vector<std::string> toolNamesByCategory(ToolCategory category) const override;

    // ── 渐进式加载控制 ──

    // 生成延迟工具的文本摘要（注入 system prompt，告知 LLM 有哪些工具可搜索）。
    std::string deferredToolsStub() const;

    // 重置加载状态（新会话时调用）。
    void reset();

    // 当前已加载工具数（含核心 + tool_search + 动态加载）。
    size_t loadedCount() const;

    // 启用/禁用渐进式加载（false = 全量模式，getDefinitions 返回全部工具）。
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    ToolRegistry& registry_;
    std::set<std::string> loaded_tools_;
    mutable std::shared_mutex mutex_;
    bool enabled_ = true;

    nlohmann::json executeToolSearch(const nlohmann::json& args);
    void initCoreTools();
    static llm::ToolDefinition toolSearchDefinition();
};

} // namespace agent
