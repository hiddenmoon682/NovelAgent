#pragma once

/// ContextManager 相关类型定义 — 上下文组装结果的 DTO。

#include "llm/Message.h"

#include <string>
#include <vector>

namespace agent {

/// 上下文组装结果（简化版 — 移除了未使用的 budget/degradation_level 字段）。
struct ContextAssembly {
    std::vector<llm::Message> messages;   ///< 截断后的消息列表
    std::string system_prompt;            ///< 动态系统提示词（项目上下文等）
    int total_tokens = 0;                 ///< system_prompt + messages 的总 token 数
    int truncated_count = 0;              ///< 被截断的消息数（0 = 未截断）
};

} // namespace agent
