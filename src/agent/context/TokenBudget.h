#pragma once

#include "agent/context/ContextManagerTypes.h"

namespace agent {

// Token 预算配置 + 用量评估。纯值类型，无运行时状态。
// 累计统计（total_input/output）属于 SessionMeta（持久化层），不在此处。
struct TokenBudget {
    int model_limit = 131072;
    int warning_pct = 60;
    int critical_pct = 85;
    int compact_pct = 95;

    ContextStatus evaluate(int total_tokens) const {
        if (model_limit <= 0) return ContextStatus::Normal;
        if (total_tokens > model_limit) return ContextStatus::Error;
        int pct = (total_tokens * 100) / model_limit;
        if (pct >= compact_pct) return ContextStatus::AutoCompact;
        if (pct >= critical_pct) return ContextStatus::Critical;
        if (pct >= warning_pct) return ContextStatus::Warning;
        return ContextStatus::Normal;
    }

    bool needsCompaction(int total_tokens) const {
        return evaluate(total_tokens) >= ContextStatus::AutoCompact;
    }

    int usagePercent(int total_tokens) const {
        if (model_limit <= 0) return 0;
        return (total_tokens * 100) / model_limit;
    }
};

} // namespace agent
