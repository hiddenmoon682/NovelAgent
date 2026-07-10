#pragma once

// Agent 执行轨迹记录器 — Phase 5.6。
//
// 结构化记录每次 Agent 决策步骤，支持回放和调试。
// 轨迹保存为 .novelagent/traces/{session_id}.jsonl（每行一条 JSON）。
//
// 使用示例:
//   ExecutionTracer tracer;
//   tracer.record({.event_type="llm_call", .tokens_used=1500, .duration_ms=3200});
//   tracer.dump(".novelagent/traces/");

#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <chrono>

namespace agent {

// 单条执行轨迹。
struct TraceEntry {
    std::string timestamp;       // ISO 8601 UTC 时间戳
    int step_index = 0;          // 步骤序号
    std::string event_type;      // user_input | llm_call | tool_call | tool_result | llm_response | error
    nlohmann::json payload;      // 事件详情（不含完整章节内容，只含摘要）
    int tokens_used = 0;         // 该步骤消耗的 token 数
    int duration_ms = 0;         // 该步骤耗时（毫秒）

    // 便捷工厂。
    static TraceEntry make(const std::string& type, int tokens = 0, int ms = 0);
};

// 执行轨迹记录器。
class ExecutionTracer {
public:
    ExecutionTracer() = default;

    // 记录一条轨迹。
    void record(const TraceEntry& entry);

    // 记录一条简单轨迹（便捷接口）。
    void record(const std::string& event_type, int tokens = 0, int duration_ms = 0,
                const nlohmann::json& payload = {});

    // 获取所有记录（只读）。
    const std::vector<TraceEntry>& entries() const { return entries_; }

    // 记录总数。
    int count() const { return static_cast<int>(entries_.size()); }

    // 保存轨迹到 .novelagent/traces/{session_id}.jsonl。
    // @param dir_path  轨迹目录（通常是项目的 .novelagent/traces/ 目录）
    // @return          保存的文件路径
    std::string dump(const std::string& dir_path) const;

    // 汇总统计（供 /trace stats 使用）。
    nlohmann::json summary() const;

    // 清空所有记录。
    void clear() { entries_.clear(); step_index_ = 0; }

    // 最近 N 条轨迹的文本摘要（供 /trace show 使用）。
    std::string recentSummary(int n = 10) const;

private:
    // 轨迹条目容器 — 按记录顺序存储所有 TraceEntry。
    // 每次 record() 在末尾 push_back 一条，entries() 返回只读引用供外部遍历。
    // dump() 遍历此容器逐行写出为 JSONL 文件。
    // clear() 清空容器并将 step_index_ 重置为 0。
    std::vector<TraceEntry> entries_;

    // 步骤序号计数器 — 从 0 开始，每次 record() 自动递增。
    // 注意：step_index_ 仅由便捷接口 record(event_type, tokens, ms, payload)
    // 内部自增；接受 TraceEntry 的接口不会自动设置 step_index_，
    // 调用方需自行填充 TraceEntry::step_index 字段。
    int step_index_ = 0;

    // 生成当前 UTC 时间戳。
    static std::string nowTimestamp();

    // 生成 session ID。
    static std::string generateSessionId();
};

} // namespace agent
