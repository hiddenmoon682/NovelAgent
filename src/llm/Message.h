#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ============================================================================
// OpenAI 兼容 Chat Completions API 的消息数据结构
// 兼容 DeepSeek / Kimi / Claude 三家 API（均采用 OpenAI 格式）
// ============================================================================

namespace llm {

/// 消息发送者角色（OpenAI Chat Completions 标准四角色）
enum class MessageRole {
    System,     // 系统提示词（设定 AI 行为边界）
    User,       // 用户输入
    Assistant,  // AI 回复
    Tool        // 工具调用结果（回传给 AI）
};

/// LLM 请求调用的工具（function calling）
struct ToolCall {
    std::string id;             // 工具调用唯一标识（由 API 返回）
    std::string type;           // 调用类型，通常为 "function"
    std::string function_name;  // 函数名
    std::string arguments;      // 函数参数的 JSON 字符串
};

/// 单条对话消息
struct Message {
    MessageRole role = MessageRole::User;
    std::string content;               // 消息正文（可为空，当 tool_calls 非空时）
    std::vector<ToolCall> tool_calls;  // 工具调用列表（仅 assistant 角色使用）
    std::string tool_call_id;          // 关联的工具调用 ID（仅 tool 角色使用）
    std::string name;                  // 可选参与者名称
};

/// 工具定义（注册到 ToolRegistry 后发给 LLM）
struct ToolDefinition {
    std::string name;              // 函数名
    std::string description;       // 功能描述（告诉 LLM 何时调用）
    nlohmann::json parameters;     // JSON Schema 参数定义
};

/// LLM API 调用返回结果
struct LLMResponse {
    std::string content;               // 完整文本回复
    std::vector<ToolCall> tool_calls;  // 工具调用列表
    std::string finish_reason;         // 结束原因: "stop" / "length" / "tool_calls" / "content_filter"
    std::string model;                 // 实际使用的模型名
    int prompt_tokens = 0;             // 输入 token 数
    int completion_tokens = 0;         // 输出 token 数
};

} // namespace llm
