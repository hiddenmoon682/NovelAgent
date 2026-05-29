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

// 角色 ↔ 字符串 转换，供 to_json/from_json 使用
inline std::string roleToString(MessageRole role) {
    switch (role) {
        case MessageRole::System:    return "system";
        case MessageRole::User:      return "user";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::Tool:      return "tool";
    }
    return "user";
}

inline MessageRole roleFromString(const std::string& s) {
    if (s == "system")    return MessageRole::System;
    if (s == "user")      return MessageRole::User;
    if (s == "assistant") return MessageRole::Assistant;
    if (s == "tool")      return MessageRole::Tool;
    return MessageRole::User;
}

// ============================================================================
// ToolCall — LLM 请求调用的工具（function calling）
// ============================================================================

struct ToolCall {
    std::string id;             // 工具调用唯一标识（由 API 返回）
    std::string type;           // 调用类型，通常为 "function"
    std::string function_name;  // 函数名
    std::string arguments;      // 函数参数的 JSON 字符串
};

// ToolCall 的 JSON 序列化。
// 将展平字段还原为 OpenAI 嵌套格式：{ id, type, function: { name, arguments } }
inline void to_json(nlohmann::json& j, const ToolCall& tc) {
    j = nlohmann::json{
        {"id", tc.id},
        {"type", tc.type.empty() ? "function" : tc.type},
        {"function", {
            {"name", tc.function_name},
            {"arguments", tc.arguments}
        }}
    };
}

// ToolCall 的 JSON 反序列化。
// 从 OpenAI 嵌套格式提取到展平字段。
inline void from_json(const nlohmann::json& j, ToolCall& tc) {
    tc.id = j.value("id", "");
    tc.type = j.value("type", "function");
    if (j.contains("function") && j["function"].is_object()) {
        const auto& func = j["function"];
        tc.function_name = func.value("name", "");
        tc.arguments = func.value("arguments", "");
    }
}

// ============================================================================
// Message — 单条对话消息
// ============================================================================

struct Message {
    MessageRole role = MessageRole::User;
    std::string content;               // 消息正文（可为空，当 tool_calls 非空时）
    std::vector<ToolCall> tool_calls;  // 工具调用列表（仅 assistant 角色使用）
    std::string tool_call_id;          // 关联的工具调用 ID（仅 tool 角色使用）
    std::string name;                  // 可选参与者名称
};

// Message 的 JSON 序列化。
// 仅输出非空的可选字段，避免发送空数组/空字符串到 API。
inline void to_json(nlohmann::json& j, const Message& msg) {
    j = nlohmann::json{
        {"role", roleToString(msg.role)},
        {"content", msg.content}
    };
    if (!msg.tool_calls.empty()) {
        j["tool_calls"] = msg.tool_calls;
    }
    if (!msg.tool_call_id.empty()) {
        j["tool_call_id"] = msg.tool_call_id;
    }
    if (!msg.name.empty()) {
        j["name"] = msg.name;
    }
}

// Message 的 JSON 反序列化。
// 缺失的可选字段回落到默认空值。
inline void from_json(const nlohmann::json& j, Message& msg) {
    msg.role = roleFromString(j.value("role", "user"));
    msg.content = j.value("content", "");
    if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
        msg.tool_calls = j["tool_calls"].get<std::vector<ToolCall>>();
    } else {
        msg.tool_calls.clear();
    }
    msg.tool_call_id = j.value("tool_call_id", "");
    msg.name = j.value("name", "");
}

// ============================================================================
// ToolDefinition — 注册到 ToolRegistry 后发给 LLM
// ============================================================================

struct ToolDefinition {
    std::string name;              // 函数名
    std::string description;       // 功能描述（告诉 LLM 何时调用）
    nlohmann::json parameters;     // JSON Schema 参数定义
};

// ToolDefinition 的 JSON 序列化（OpenAI function calling 格式）。
inline void to_json(nlohmann::json& j, const ToolDefinition& td) {
    j = nlohmann::json{
        {"type", "function"},
        {"function", {
            {"name", td.name},
            {"description", td.description},
            {"parameters", td.parameters}
        }}
    };
}

// ToolDefinition 的 JSON 反序列化。
inline void from_json(const nlohmann::json& j, ToolDefinition& td) {
    if (j.contains("function") && j["function"].is_object()) {
        const auto& func = j["function"];
        td.name = func.value("name", "");
        td.description = func.value("description", "");
        td.parameters = func.value("parameters", nlohmann::json::object());
    } else {
        // 兼容展平格式
        td.name = j.value("name", "");
        td.description = j.value("description", "");
        td.parameters = j.value("parameters", nlohmann::json::object());
    }
}

// ============================================================================
// LLMResponse — LLM API 调用返回结果
// ============================================================================

struct LLMResponse {
    // ── 元数据 ──
    std::string id;                    // 响应唯一 ID，例如 "chatcmpl-xxx"，用于日志追踪
    int64_t created = 0;               // Unix 时间戳
    std::string model;                 // 实际使用的模型名
    std::string system_fingerprint;    // 系统指纹（后端配置标识，可用于变更检测）

    // ── 内容 ──
    std::string content;               // 完整文本回复
    std::string reasoning_content;     // DeepSeek 特有 — 思维链推理过程（deepseek-reasoner / thinking 模式）
    std::vector<ToolCall> tool_calls;  // 工具调用列表
    std::string finish_reason;         // 结束原因: "stop" / "length" / "tool_calls" / "content_filter"

    // ── Token 统计 ──
    int prompt_tokens = 0;             // 输入 token 数
    int completion_tokens = 0;         // 输出 token 数
    int total_tokens = 0;              // 总 token 数（省去手动相加）
    int cached_tokens = 0;             // prompt_tokens_details.cached_tokens — 缓存命中的 prompt token 数
    int reasoning_tokens = 0;          // completion_tokens_details.reasoning_tokens — 思维链消耗的 token 数
};

// LLMResponse 的 JSON 序列化（OpenAI API 完整响应格式，保证 to_json/from_json 对称）。
inline void to_json(nlohmann::json& j, const LLMResponse& r) {
    j = nlohmann::json{
        {"id", r.id},
        {"object", "chat.completion"},
        {"created", r.created},
        {"model", r.model},
        {"system_fingerprint", r.system_fingerprint}
    };

    // choices[0]
    nlohmann::json message;
    message["role"] = "assistant";
    message["content"] = r.content;
    if (!r.reasoning_content.empty()) {
        message["reasoning_content"] = r.reasoning_content;
    }
    if (!r.tool_calls.empty()) {
        message["tool_calls"] = r.tool_calls;
    }
    j["choices"] = nlohmann::json::array({
        {
            {"index", 0},
            {"message", message},
            {"finish_reason", r.finish_reason}
        }
    });

    // usage
    j["usage"] = {
        {"prompt_tokens", r.prompt_tokens},
        {"completion_tokens", r.completion_tokens},
        {"total_tokens", r.total_tokens}
    };
    if (r.cached_tokens > 0 || r.reasoning_tokens > 0) {
        j["usage"]["prompt_tokens_details"] = {{"cached_tokens", r.cached_tokens}};
        j["usage"]["completion_tokens_details"] = {{"reasoning_tokens", r.reasoning_tokens}};
    }
}

// LLMResponse 的 JSON 反序列化（从 OpenAI API 原始响应 JSON 提取）。
inline void from_json(const nlohmann::json& j, LLMResponse& r) {
    // 顶层元数据
    r.id = j.value("id", "");
    r.model = j.value("model", "");
    r.created = j.value("created", 0);
    r.system_fingerprint = j.value("system_fingerprint", "");

    // choices[0].message
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
        const auto& choice = j["choices"][0];
        r.finish_reason = choice.value("finish_reason", "");

        if (choice.contains("message") && choice["message"].is_object()) {
            const auto& msg = choice["message"];
            r.content = msg.value("content", "");
            r.reasoning_content = msg.value("reasoning_content", "");

            if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                r.tool_calls = msg["tool_calls"].get<std::vector<ToolCall>>();
            } else {
                r.tool_calls.clear();
            }
        }
    }

    // usage
    if (j.contains("usage") && j["usage"].is_object()) {
        const auto& usage = j["usage"];
        r.prompt_tokens = usage.value("prompt_tokens", 0);
        r.completion_tokens = usage.value("completion_tokens", 0);
        r.total_tokens = usage.value("total_tokens", 0);

        if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].is_object()) {
            r.cached_tokens = usage["prompt_tokens_details"].value("cached_tokens", 0);
        }
        if (usage.contains("completion_tokens_details") && usage["completion_tokens_details"].is_object()) {
            r.reasoning_tokens = usage["completion_tokens_details"].value("reasoning_tokens", 0);
        }
    }
}

} // namespace llm
