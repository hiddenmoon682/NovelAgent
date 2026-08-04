#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ============================================================================
// OpenAI 兼容 Chat Completions API 的消息数据结构
// 兼容 DeepSeek / Kimi / Claude 三家 API（均采用 OpenAI 格式）
// ============================================================================

namespace llm {

// 消息发送者角色（OpenAI Chat Completions 标准四角色）
enum class MessageRole {
    System,     // 系统提示词（设定 AI 行为边界）
    User,       // 用户输入
    Assistant,  // AI 回复
    Tool        // 工具调用结果（回传给 AI）
};

// 角色 ↔ 字符串 转换，供 to_json/from_json 使用。
//
// @param role 消息角色枚举。
// @return OpenAI 角色字符串（"system"/"user"/"assistant"/"tool"）。
inline std::string roleToString(MessageRole role) {
    switch (role) {
        case MessageRole::System:    return "system";
        case MessageRole::User:      return "user";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::Tool:      return "tool";
    }
    return "user";
}

// 角色字符串 → 枚举转换。
//
// @param s OpenAI 角色字符串。
// @return 对应的枚举值；未知字符串回落为 MessageRole::User。
inline MessageRole roleFromString(const std::string& s) {
    if (s == "system")    return MessageRole::System;
    if (s == "user")      return MessageRole::User;
    if (s == "assistant") return MessageRole::Assistant;
    if (s == "tool")      return MessageRole::Tool;
    return MessageRole::User;
}

// null 安全的字符串字段读取：键缺失或值为 null 时回落默认值。
//
// WHY：OpenAI 协议中 assistant+tool_calls 消息的 content 合法值为 null
// （本文件 to_json 也按此输出），而 j.value() 遇显式 null 会抛
// type_error.302，导致序列化 round-trip 及真实 API 响应解析失败，
// 必须先判 is_null 再取值。
//
// @param j        输入 JSON 对象。
// @param key      字段名。
// @param fallback 键缺失或为 null 时的默认值。
// @return 字段值或默认值；字段存在但为非字符串非 null 类型时仍抛异常（与原行为一致）。
inline std::string getStringOrDefault(const nlohmann::json& j, const char* key,
                                      const std::string& fallback = "") {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    return it->get<std::string>();
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

// ToolCall 的 JSON 序列化（nlohmann ADL 钩子）。
//
// 将展平字段还原为 OpenAI 嵌套格式：{ id, type, function: { name, arguments } }；
// type 为空时补默认值 "function"。
//
// @param j  输出 JSON（被覆盖写入）。
// @param tc 待序列化的工具调用。
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

// ToolCall 的 JSON 反序列化（nlohmann ADL 钩子）。
//
// 从 OpenAI 嵌套格式提取到展平字段；缺失字段回落到空值，不抛异常。
//
// @param j  输入 JSON（OpenAI tool_call 对象）。
// @param tc 输出的工具调用对象（被覆盖写入）。
inline void from_json(const nlohmann::json& j, ToolCall& tc) {
    // WHY：统一用 null 安全读取，部分 provider 对可选字段会返回显式 null。
    tc.id = getStringOrDefault(j, "id");
    tc.type = getStringOrDefault(j, "type", "function");
    if (j.contains("function") && j["function"].is_object()) {
        const auto& func = j["function"];
        tc.function_name = getStringOrDefault(func, "name");
        tc.arguments = getStringOrDefault(func, "arguments");
    }
}

// ============================================================================
// Message — 单条对话消息
// ============================================================================

struct Message {
    // ── 字段 ──
    MessageRole role = MessageRole::User;
    std::string content;               // 消息正文（可为空，当 tool_calls 非空时）
    std::vector<ToolCall> tool_calls;  // 工具调用列表（仅 assistant 角色使用）
    std::string tool_call_id;          // 关联的工具调用 ID（仅 tool 角色使用）
    std::string name;                  // 可选参与者名称
    std::string reasoning_content;     // DeepSeek thinking 模式的推理过程（工具调用循环中使用）
    bool preserved = false;            // 内部标记：截断时优先保留（不参与 JSON 序列化）
    bool is_control = false;           // 控制消息标记（P6）：系统注入的占位消息（如取消提示），
                                       // 非真实对话；UI 据此过滤/特殊渲染。不进入 OpenAI 协议序列化。

    // ── 便捷工厂方法 ──

    // 创建用户消息（最常用）
    static Message user(std::string content) {
        Message m;
        m.role = MessageRole::User;
        m.content = std::move(content);
        return m;
    }

    // 创建系统提示词消息
    static Message system(std::string content) {
        Message m;
        m.role = MessageRole::System;
        m.content = std::move(content);
        return m;
    }

    // 创建 AI 助手消息
    static Message assistant(std::string content) {
        Message m;
        m.role = MessageRole::Assistant;
        m.content = std::move(content);
        return m;
    }

    // 创建工具调用结果消息（回传给 LLM）
    static Message toolResult(std::string call_id, std::string content) {
        Message m;
        m.role = MessageRole::Tool;
        m.tool_call_id = std::move(call_id);
        m.content = std::move(content);
        return m;
    }
};

// Message 的 JSON 序列化（nlohmann ADL 钩子）。
//
// 仅输出非空的可选字段，避免发送空数组/空字符串到 API；
// preserved 为内部标记，不参与序列化。
//
// @param j   输出 JSON（被覆盖写入）。
// @param msg 待序列化的消息。
inline void to_json(nlohmann::json& j, const Message& msg) {
    // content 为空但 tool_calls 非空时 → 输出 null（OpenAI API 要求）
    nlohmann::json content_val = (msg.content.empty() && !msg.tool_calls.empty())
        ? nlohmann::json(nullptr) : nlohmann::json(msg.content);
    j = nlohmann::json{
        {"role", roleToString(msg.role)},
        {"content", content_val}
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
    if (!msg.reasoning_content.empty()) {
        j["reasoning_content"] = msg.reasoning_content;
    }
}

// Message 的 JSON 反序列化（nlohmann ADL 钩子）。
//
// 缺失或为 null 的可选字段回落到默认空值，未知 role 回落为 user。
// WHY：to_json 对 assistant+tool_calls 的空 content 输出 null（OpenAI 协议），
// 反序列化必须容忍 null 才能保证 round-trip 不抛异常。
//
// @param j   输入 JSON（OpenAI message 对象）。
// @param msg 输出的消息对象（被覆盖写入）。
inline void from_json(const nlohmann::json& j, Message& msg) {
    msg.role = roleFromString(getStringOrDefault(j, "role", "user"));
    msg.content = getStringOrDefault(j, "content");
    if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
        msg.tool_calls = j["tool_calls"].get<std::vector<ToolCall>>();
    } else {
        msg.tool_calls.clear();
    }
    msg.tool_call_id = getStringOrDefault(j, "tool_call_id");
    msg.name = getStringOrDefault(j, "name");
    msg.reasoning_content = getStringOrDefault(j, "reasoning_content");
}

// ============================================================================
// ToolDefinition — 注册到 ToolRegistry 后发给 LLM
// ============================================================================

struct ToolDefinition {
    std::string name;              // 函数名
    std::string description;       // 功能描述（告诉 LLM 何时调用）
    nlohmann::json parameters;     // JSON Schema 参数定义
};

// ToolDefinition 的 JSON 序列化（OpenAI function calling 格式，nlohmann ADL 钩子）。
//
// @param j  输出 JSON（被覆盖写入）。
// @param td 待序列化的工具定义。
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

// ToolDefinition 的 JSON 反序列化（nlohmann ADL 钩子）。
//
// 优先按 OpenAI 嵌套格式（function 对象）解析，否则兼容展平格式。
//
// @param j  输入 JSON。
// @param td 输出的工具定义对象（被覆盖写入）。
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

// LLMResponse 的 JSON 序列化（nlohmann ADL 钩子）。
//
// 输出 OpenAI API 完整响应格式（含 choices[0] 与 usage），
// 保证 to_json/from_json 对称，可用于响应的落盘与回放。
//
// @param j 输出 JSON（被覆盖写入）。
// @param r 待序列化的响应。
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

// LLMResponse 的 JSON 反序列化（nlohmann ADL 钩子）。
//
// 从 OpenAI API 原始响应 JSON 提取 choices[0].message 与 usage；
// 缺失字段回落到默认值，不抛异常。
//
// @param j 输入 JSON（API 原始响应）。
// @param r 输出的响应对象（被覆盖写入）。
inline void from_json(const nlohmann::json& j, LLMResponse& r) {
    // 顶层元数据
    // WHY：OpenAI 协议中 system_fingerprint 可为 null，统一 null 安全读取。
    r.id = getStringOrDefault(j, "id");
    r.model = getStringOrDefault(j, "model");
    r.created = j.value("created", 0);
    r.system_fingerprint = getStringOrDefault(j, "system_fingerprint");

    // choices[0].message
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
        const auto& choice = j["choices"][0];
        r.finish_reason = getStringOrDefault(choice, "finish_reason");

        if (choice.contains("message") && choice["message"].is_object()) {
            const auto& msg = choice["message"];
            // WHY：真实 API（OpenAI/DeepSeek）对工具调用响应返回 content: null，
            // 必须 null 容错，否则 chatNonStreaming 无法解析此类响应。
            r.content = getStringOrDefault(msg, "content");
            r.reasoning_content = getStringOrDefault(msg, "reasoning_content");

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

// 流式响应中间类型（ToolCallDelta / UsageInfo / StreamChunk）
// 从 Message.h 拆分出，此处包含以保持向后兼容
#include "llm/StreamingTypes.h"
