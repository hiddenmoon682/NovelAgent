#pragma once

// ContextManager 相关类型定义 — 上下文组装结果 + 会话级统计 DTO。

#include "llm/Message.h"

#include <string>
#include <vector>

namespace agent {

// ===========================================================================
// 上下文组装结果
// ===========================================================================

// 上下文组装结果。
struct ContextAssembly {
    std::vector<llm::Message> messages;     //  截断后的消息列表
    std::string system_prompt;              //  动态系统提示词（项目上下文 + 压缩摘要等）
    int total_tokens = 0;                   //  system_prompt + messages 的总 token 数
    int truncated_count = 0;                //  被截断的消息数（0 = 未截断）
    bool has_compacted_context = false;     //  是否注入了压缩摘要
    bool has_semantic_context = false;       //  A3: 是否注入了语义检索结果
    std::vector<std::string> warnings;      //  降级/问题警告列表（供 CLI/TUI 展示）
};

// ===========================================================================
// 会话级 Token 统计
// ===========================================================================

// 会话级累计 token 状态（跨多次 assemble() 调用）。
struct SessionTokenState {
    int total_input_tokens = 0;             //  跨所有请求的累计输入 token
    int total_output_tokens = 0;            //  跨所有请求的累计输出 token
    int request_count = 0;                  //  会话中的请求数
    int model_context_limit = 131072;       //  模型上下文窗口上限（从 ProviderConfig 获取）
};

// 上下文用量状态枚举。
enum class ContextStatus { Normal, Warning, Critical };

// 请求前检查结果（供调用方决定是否需要压缩）。
struct PreRequestResult {
    ContextStatus status = ContextStatus::Normal;
    int usage_percent = 0;                  //  当前用量百分比 [0, 100]
    int estimated_tokens = 0;               //  当前估算的上下文 token 数
    int model_limit = 131072;               //  模型窗口上限
};

// ===========================================================================
// Compaction 结果
// ===========================================================================

// Compaction 结果（手动触发 /compact 命令的返回值）。
struct CompactResult {
    std::string summary;                    //  LLM 生成的摘要文本
    int messages_compacted = 0;             //  被摘要的消息数
    int tokens_before = 0;                  //  压缩前估算 token 数
    int tokens_after = 0;                   //  压缩后估算 token 数
};

} // namespace agent
