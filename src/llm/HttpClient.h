#pragma once

// 共享 HTTP 客户端 — 所有 LLM API 调用的 HTTP 基础设施。
//
// 封装 URL 解析、Bearer Token 认证、指数退避重试、错误响应解析。
// LLMClient（Chat API）和 EmbeddingGenerator（Embeddings API）共享此实现。
//
// 线程安全：单实例不安全（httplib::Client 内部状态不可并发）。
// 多线程场景请通过 LLMClientFactory 为每个执行上下文创建独立的 LLMClient（从而独立的 HttpClient）。

#include <nlohmann/json_fwd.hpp>
#include <httplib.h>

#include <functional>
#include <string>
#include <memory>

namespace llm {

// HTTP 客户端配置。
struct HttpConfig {
    std::string base_url;        // 如 "https://api.deepseek.com"
    std::string api_key;         // Bearer Token
    int connect_timeout = 30;    // 连接超时（秒）
    int read_timeout = 60;       // 读取超时（秒）
    int max_retries = 3;         // 最大重试次数
    int retry_base_delay_ms = 1000; // 指数退避基数（毫秒）
};

// 共享 HTTP 客户端 — 封装通用的 HTTP 调用模式。
//
// 使用方式：
//   1. LLMClient 持有 HttpClient 实例（组合），用于 Chat API 调用。
//   2. EmbeddingGenerator 持有 HttpClient 实例，用于 Embeddings API 调用。
class HttpClient {
public:
    // 从配置构造，解析 base_url 为 host + path_prefix。
    explicit HttpClient(const HttpConfig& config);

    ~HttpClient();

    // ── 简单 JSON POST（非流式）─────────────────────────────

    // 发送 POST JSON 请求，自动重试，返回解析后的 JSON 响应体。
    //
    // @param path  API 路径（如 "/v1/chat/completions"）
    // @param body  请求体 JSON
    // @return      API 返回的 JSON（HTTP 200 的 body）
    // @throws std::runtime_error 网络错误、API 错误、JSON 解析错误
    nlohmann::json post(const std::string& path,
                        const nlohmann::json& body);

    // ── 流式 POST（供 LLMClient::chat 使用）────────────────

    // 发送流式 POST 请求（通过 content_receiver 接收增量数据）。
    //
    // @param path             API 路径
    // @param body             请求体 JSON 字符串
    // @param content_receiver 接收流式数据的回调
    // @return                 HTTP 响应结果（调用方检查 res.error() 和 res->status）
    httplib::Result postStreaming(
        const std::string& path,
        const std::string& body,
        std::function<bool(const char* data, size_t len)> content_receiver);

    // ── 查询 ──

    // 返回解析后的 host 部分。
    const std::string& host() const { return host_; }

    // 返回 API Key（用于构造 Authorization 头）。
    const std::string& apiKey() const { return config_.api_key; }

    // 返回底层 httplib::Client（用于高级操作如连接池管理）。
    httplib::Client& rawClient();

    // ── 静态工具函数（供调用方在 HttpClient 外部使用）──

    // 解析 API 错误响应 JSON。
    static std::string parseApiError(int http_status, const std::string& response_body);

    // 将 httplib::Error 枚举转换为中文错误描述。
    static std::string httpErrorToString(int error_code);

    // 判断 HTTP 状态码是否可重试。
    static bool isRetryableStatus(int status);

    // 判断网络错误是否可重试。
    static bool isRetryableNetworkError(int err);

private:
    HttpConfig config_;          // HTTP 客户端配置
    std::string host_;           // 解析后的主机名
    std::string path_prefix_;    // base_url 中的路径前缀（如 "/v1" 省略）
    std::string scheme_;         // "https" 或 "http"
    std::unique_ptr<httplib::Client> client_;

    // 解析 base_url 为 scheme + host + path_prefix。
    void parseUrl();

    // 构造完整的请求路径（path_prefix + path）。
    std::string fullPath(const std::string& path) const;

    // 构造 Authorization + Content-Type 等标准请求头。
    httplib::Headers defaultHeaders() const;

};

} // namespace llm
