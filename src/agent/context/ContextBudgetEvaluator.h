#pragma once

#include "agent/context/TokenBudget.h"

#include <string>
#include <vector>

namespace llm {
class IMemory;
class TokenCounter;
}

namespace agent {

// Token 预算评估结果。
struct BudgetEvaluationResult {
    int message_tokens = 0;              // 对话历史消息的 token 数
    int total_tokens = 0;                // 总 token 数（message_tokens）
    ContextStatus status = ContextStatus::Normal;  // 上下文用量状态
    std::vector<std::string> warnings;   // 警告或错误信息列表
    bool fatal = false;                  // true 表示已达致命上限，不应继续调用 LLM
};

// 无状态 Token 预算评估器。
// 每次调用时从 Memory 读取消息列表，统计 token 数并评估上下文用量状态。
// 不修改 Memory、不触发 compaction（纯计算）。
class ContextBudgetEvaluator {
public:
    // 评估上下文：统计 token 数、判断用量状态。
    // memory:        会话记忆，提供消息列表
    // budget:        token 预算，用于评估上下文用量是否超限
    // system_prompt: 实际发送给 API 的完整 system prompt（含静态部分和动态附加文本）
    // model_name:    模型名，用于校准 token 计数
    // calibrator:    可选的 TokenCounter 校准器
    BudgetEvaluationResult evaluate(const llm::IMemory& memory,
                                    const TokenBudget& budget,
                                    const std::string& system_prompt,
                                    const std::string& model_name = {},
                                    const llm::TokenCounter* calibrator = nullptr) const;
};

} // namespace agent
