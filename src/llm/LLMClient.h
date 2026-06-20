#pragma once

#include "config/AppConfig.h"
#include "llm/HttpClient.h"
#include "llm/ILLMClient.h"
#include "llm/Message.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llm {

/// LLM Chat Completion 客户端 — HTTP + SSE 实现。
/// 封装 OpenAI 兼容格式的 HTTP POST 请求，支持流式和非流式两种模式。
///
/// Phase 4 架构改进：通过组合 HttpClient 复用 HTTP 基础设施（URL 解析/认证/重试），
/// 避免与 EmbeddingGenerator 之间的代码重复。
///
/// 生命周期：由 Agent 持有，每次对话轮次调用 chat()。
/// LLMClient 自身不维护对话历史——历史由 Agent 维护。
///
/// 线程安全：单实例不安全（httplib 内部状态不可共享）。
/// 多线程场景请使用 LLMClientFactory 为每个执行上下文创建独立实例。
class LLMClient : public ILLMClient {
public:
    /// 构造函数接收 ProviderConfig，拷贝保存。
    /// 不发起网络请求。API Key 有效性在第一次 chat() 调用时验证。
    explicit LLMClient(const ProviderConfig& config);

    // ================================================================
    // 核心 API
    // ================================================================

    /// 流式调用：实时通过 callbacks 输出，请求完成后返回完整 LLMResponse。
    LLMResponse chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools = {},
        const std::string& system_prompt = "",
        StreamCallbacks callbacks = {}
    );

    /// 非流式调用：等待完整 JSON 响应后返回。
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

    /// 返回最近一次调用的错误详情
    std::string lastError() const { return last_error_; }

private:
    // ── LLM 提供商配置（model / api_key / base_url 等），构造函数中拷贝保存 ──
    ProviderConfig config_;

    // ── 共享的 HTTP 客户端，封装了 URL 解析 / 认证 / 重试等基础设施 ──
    // 由 HttpClient 提供 post() 和 postStreaming() 两个核心方法 
    HttpClient http_;

    // ── 最近一次 chat() / chatNonStreaming() 调用的错误详情 ──
    // 调用方可通过 lastError() 查询，用于日志记录或用户提示
    std::string last_error_;

    /// 检查 api_key / base_url / model 等必要字段
    void validateConfig() const;

    /// 构造请求体 JSON
    nlohmann::json buildRequestBody(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools,
        const std::string& system_prompt,
        bool stream) const;
};

} // namespace llm
