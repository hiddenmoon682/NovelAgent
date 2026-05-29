#pragma once

#include "config/AppConfig.h"
#include "llm/Message.h"

#include <functional>
#include <string>
#include <vector>

namespace llm {

/// 流式调用的实时回调接口。
/// 调用方（Agent / StreamDisplay）通过这些回调获得逐 token 的输出。
struct StreamCallbacks {
    /// 文本内容增量（每次收到 content delta 时触发）
    std::function<void(const std::string& delta)> on_content;

    /// 思维链增量（DeepSeek reasoning_content）
    std::function<void(const std::string& delta)> on_reasoning;

    /// 首次检测到 tool_call delta 时触发（用于显示"正在调用工具..."等提示）
    std::function<void()> on_tool_call_start;

    /// 流式完成后触发，携带完整 LLMResponse（与 chat() 返回值相同）
    std::function<void(const LLMResponse& response)> on_complete;

    /// 流式过程中发生错误时触发
    std::function<void(const std::string& error)> on_error;
};

/// LLM Chat Completion 客户端。
/// 封装 OpenAI 兼容格式的 HTTP POST 请求，支持流式和非流式两种模式。
///
/// 生命周期：由 Agent 持有，每次对话轮次调用 chat()。
/// LLMClient 自身不维护对话历史——历史由 Agent 维护。
///
/// 线程安全：不安全。同一实例不应并发调用。
class LLMClient {
public:
    /// 构造函数接收 ProviderConfig，拷贝保存。
    /// 不发起网络请求。API Key 有效性在第一次 chat() 调用时验证。
    explicit LLMClient(const ProviderConfig& config);

    // ================================================================
    // 核心 API
    // ================================================================

    /// 流式调用：实时通过 callbacks 输出，请求完成后返回完整 LLMResponse。
    ///
    /// @param messages      对话历史（不含 system 消息）
    /// @param tools         可供 LLM 调用的工具定义列表
    /// @param system_prompt 系统提示词（非空时作为首条 system 消息自动插入）
    /// @param callbacks     流式回调（可为空，仅返回完整结果，无实时输出）
    /// @return              完整 LLMResponse（含 content, tool_calls, usage）
    /// @throws std::runtime_error 网络错误、API 错误、JSON 解析错误
    LLMResponse chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = "",
        StreamCallbacks callbacks = {}
    );

    /// 非流式调用：等待完整 JSON 响应后返回。
    /// 用于 --exec 单次命令模式或不需要实时输出的场景。
    LLMResponse chatNonStreaming(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = ""
    );

    // ================================================================
    // 辅助查询
    // ================================================================

    /// 返回当前使用的 ProviderConfig
    const ProviderConfig& config() const { return config_; }

    /// 返回最近一次调用的错误详情（异常 what() 之外的额外上下文）
    std::string lastError() const { return last_error_; }

private:
    ProviderConfig config_;
    std::string last_error_;

    // 超时配置（秒）
    static constexpr int kConnectionTimeout = 30;
    static constexpr int kReadTimeout = 120; // 流式模式需较长超时

    /// 检查 api_key / base_url / model 等必要字段
    void validateConfig() const;

    /// 构造请求体 JSON
    nlohmann::json buildRequestBody(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools,
        const std::string& system_prompt,
        bool stream) const;

    /// 从 HTTP 错误响应中提取人类可读的错误消息
    /// 优先解析 JSON error.message，回退到 HTTP 状态码描述
    std::string parseApiError(int http_status, const std::string& response_body) const;

    /// 将 httplib::Error 枚举转换为中文错误描述
    static std::string httpErrorToString(int error_code);
};

} // namespace llm
