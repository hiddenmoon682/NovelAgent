#pragma once

#include "agent/context/ContextManagerTypes.h"

namespace agent {

// Token 预算配置 + 用量评估。纯值类型，无运行时状态。
struct TokenBudget {
    int model_limit = 131072;    // 模型上下文上限（token），默认约等于 128K 模型
    int warning_pct = 60;        // 触发 Warning 状态的百分比阈值
    int critical_pct = 70;       // 触发 Critical 状态的百分比阈值
    int compact_pct = 80;        // 触发 AutoCompact 状态的百分比阈值

    // 根据总 token 数评估上下文状态：
    //   Error        → 超过 model_limit，应阻止 LLM 调用
    //   AutoCompact  → >= compact_pct，应自动触发压缩
    //   Critical     → >= critical_pct，接近上限
    //   Warning      → >= warning_pct，可考虑压缩
    //   Normal       → 低于所有阈值
    ContextStatus evaluate(int total_tokens) const {
        if (model_limit <= 0) return ContextStatus::Normal;
        if (total_tokens > model_limit) return ContextStatus::Error;
        int pct = (total_tokens * 100) / model_limit;
        if (pct >= compact_pct) return ContextStatus::AutoCompact;
        if (pct >= critical_pct) return ContextStatus::Critical;
        if (pct >= warning_pct) return ContextStatus::Warning;
        return ContextStatus::Normal;
    }

    // 是否达到需要执行压缩的级别（AutoCompact 及以上）
    bool needsCompaction(int total_tokens) const {
        return evaluate(total_tokens) >= ContextStatus::AutoCompact;
    }

    // 当前用量百分比（0~100），model_limit <= 0 时返回 0
    int usagePercent(int total_tokens) const {
        if (model_limit <= 0) return 0;
        return (total_tokens * 100) / model_limit;
    }
};

} // namespace agent
