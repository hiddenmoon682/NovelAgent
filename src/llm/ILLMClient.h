#pragma once

#include "config/AppConfig.h"
#include "llm/Message.h"

#include <functional>
#include <string>
#include <vector>

namespace llm {

// ===========================================================================
// StreamCallbacks — 流式调用（SSE）的实时回调集合
//
// 调用方（Agent / StreamDisplay）通过这些回调获得逐 token 的流式输出。
// 每次 LLM 返回一个数据块时，对应的回调被触发，实现"边生成边显示"的效果。
//
// 回调列表:
//   on_content        — 收到普通回复内容增量（每次一个 text token）
//   on_reasoning      — 收到思考/推理过程增量（DeepSeek R1 等模型的 CoT token）
//   on_tool_call_start— 开始调用工具（Agent 可据此切换 UI 状态）
//   on_complete       — 流式响应结束，携带完整的 LLMResponse
//   on_error          — 流式过程中发生错误，携带错误描述
//
// 所有回调均为 std::function，可为空（不设置则忽略对应事件）。
// ===========================================================================

struct StreamCallbacks {
    std::function<void(const std::string& delta)> on_content;
    std::function<void(const std::string& delta)> on_reasoning;
    std::function<void()> on_tool_call_start;
    std::function<void(const LLMResponse& response)> on_complete;
    std::function<void(const std::string& error)> on_error;
};

// ===========================================================================
// ILLMClient — LLM 客户端抽象接口（策略模式）
//
// 作用:
//   Agent 通过此接口调用 LLM，不感知底层是 HTTP/SSE/Mock。
//   新增 LLM 后端只需实现此接口，无需修改 Agent 代码。
//
// 实现类:
//   LLMClient — 基于 HttpClient 的真实 HTTP + SSE 实现
//
// 接口方法:
//   chat()              — 流式调用，通过 StreamCallbacks 实时输出增量
//   chatNonStreaming()  — 非流式调用，等待完整响应后返回
//   config()            — 返回当前使用的 ProviderConfig（只读）
//
// 参数说明:
//   messages     — 对话历史消息列表
//   tools        — 可用的工具定义（Function Calling），可选
//   system_prompt — 系统提示词，可选
//   callbacks    — 流式回调集合（仅 chat() 使用），可选
// ===========================================================================

class ILLMClient {
public:
    virtual ~ILLMClient() = default;

    // 流式调用：实时通过 callbacks 输出增量内容 / 推理过程。
    virtual LLMResponse chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = "",
        StreamCallbacks callbacks = {}) = 0;

    // 非流式调用：等待完整响应后返回 LLMResponse。
    // 适用于不需要实时显示的场景（如工具返回结果处理）。
    virtual LLMResponse chatNonStreaming(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = "") = 0;

    // 返回当前使用的 ProviderConfig（只读引用）。
    virtual const ProviderConfig& config() const = 0;
};

} // namespace llm
