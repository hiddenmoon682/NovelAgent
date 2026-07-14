#pragma once

// TokenTracker — 会话级 Token 追踪器（Issue 3 拆分自 ContextManager）。
//
// 纯计算类，无外部依赖。负责累计 LLM 调用的 token 消耗并提供用量查询。
// 可独立单元测试，无需构造 ContextManager / Project / FileStorageBackend。

#include "agent/ContextManagerTypes.h"

namespace agent {

class TokenTracker {
public:
    // 设置模型上下文窗口上限（从 ProviderConfig 获取）。
    void setModelLimit(int limit) {
        if (limit > 0) state_.model_context_limit = limit;
    }

    // 累计一次 LLM 请求的 token 消耗。
    void record(int input_tokens, int output_tokens) {
        state_.total_input_tokens += input_tokens;
        state_.total_output_tokens += output_tokens;
        state_.request_count++;
        current_context_size_ = input_tokens;
        last_output_tokens_ = output_tokens;
    }

    // 请求前检查上下文用量状态（基于最近一次请求的实际大小）。
    PreRequestResult check() const {
        PreRequestResult r;
        r.model_limit = state_.model_context_limit;
        r.estimated_tokens = current_context_size_;
        if (r.model_limit > 0) {
            r.usage_percent = (r.estimated_tokens * 100) / r.model_limit;
            if (r.estimated_tokens > r.model_limit) {
                r.status = ContextStatus::Error;
            } else if (r.usage_percent >= auto_compact_threshold_) {
                r.status = ContextStatus::AutoCompact;
            } else if (r.usage_percent >= critical_threshold_) {
                r.status = ContextStatus::Critical;
            } else if (r.usage_percent >= warning_threshold_) {
                r.status = ContextStatus::Warning;
            }
        }
        return r;
    }

    // 基于传入的实时 token 数做用量检查（替代 check()，用于调用方刚算好的数据）。
    PreRequestResult check(int realtime_total_tokens) const {
        PreRequestResult r;
        r.model_limit = state_.model_context_limit;
        r.estimated_tokens = realtime_total_tokens;
        if (r.model_limit > 0) {
            r.usage_percent = (realtime_total_tokens * 100) / r.model_limit;
            if (realtime_total_tokens > r.model_limit) {
                r.status = ContextStatus::Error;
            } else if (r.usage_percent >= auto_compact_threshold_) {
                r.status = ContextStatus::AutoCompact;
            } else if (r.usage_percent >= critical_threshold_) {
                r.status = ContextStatus::Critical;
            } else if (r.usage_percent >= warning_threshold_) {
                r.status = ContextStatus::Warning;
            }
        }
        return r;
    }

    // 返回累计统计快照。
    const SessionTokenState& snapshot() const { return state_; }
    int sessionInputTokens() const { return state_.total_input_tokens; }
    int sessionOutputTokens() const { return state_.total_output_tokens; }
    int requestCount() const { return state_.request_count; }
    int modelLimit() const { return state_.model_context_limit; }

    // 返回当前用量百分比 [0, 100]。
    int usagePercent() const {
        if (state_.model_context_limit <= 0) return 0;
        return (current_total_tokens_ * 100) / state_.model_context_limit;
    }

    // 返回最近一次请求的上下文大小（供外部使用）。
    int currentContextSize() const { return current_context_size_; }
    // 返回最近一次请求的输出 token 数。
    int lastOutputTokens() const { return last_output_tokens_; }
    // 返回当前对话的总 token 数（由 assemble() 设置，record() 不覆盖）。
    int currentTotalTokens() const { return current_total_tokens_; }
    // 手动设置当前上下文大小（供 assemble() 在 LLM 调用前写入启发式估算值）。
    void setCurrentContextSize(int size) { current_context_size_ = size; }
    // 设置当前对话的总 token 数（由 assemble() 在步骤 2 算完后写入）。
    void setCurrentTotalTokens(int total) { current_total_tokens_ = total; }

    // 重置全部会话统计。
    void reset() {
        state_ = SessionTokenState{};
        current_context_size_ = 0;
        last_output_tokens_ = 0;
        current_total_tokens_ = 0;
    }

    // Issue 3: 从持久化恢复 Token 统计（供 ContextManager::loadSessionState 使用）。
    void restore(const SessionTokenState& snapshot, int context_size = 0) {
        state_ = snapshot;
        current_context_size_ = context_size;
        last_output_tokens_ = 0;  // 最近一次输出是运行时状态，不持久化
        current_total_tokens_ = 0;
    }

    // ── 四级可配置阈值 ──
    void setWarningThreshold(int pct)      { if (pct > 0 && pct <= 100) warning_threshold_ = pct; }
    void setCriticalThreshold(int pct)     { if (pct > 0 && pct <= 100) critical_threshold_ = pct; }
    void setAutoCompactThreshold(int pct)  { if (pct > 0 && pct <= 100) auto_compact_threshold_ = pct; }
    int warningThreshold() const           { return warning_threshold_; }
    int criticalThreshold() const          { return critical_threshold_; }
    int autoCompactThreshold() const       { return auto_compact_threshold_; }

private:
    SessionTokenState state_;
    int current_context_size_ = 0;   //  最后一次请求的实际上下文 token 数
    int last_output_tokens_ = 0;     //  最后一次请求的输出 token 数
    int current_total_tokens_ = 0;   //  当前对话的总 token 数（由 assemble 设置）

    // 四级阈值（可配置，供 check()/check(int) 使用）
    int warning_threshold_ = 60;            //  Warning 阈值，默认 60%
    int critical_threshold_ = 85;           //  Critical 阈值，默认 85%
    int auto_compact_threshold_ = 95;       //  AutoCompact 阈值，默认 95%（Critical 与 Error 之间）
};

} // namespace agent
