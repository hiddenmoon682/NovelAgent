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
    //
    // 不发起网络连接；底层 httplib::Client 延迟到首次请求时建立连接。
    //
    // @param config HTTP 配置（base_url / api_key / 超时 / 重试参数），按值拷贝保存。
    explicit HttpClient(const HttpConfig& config);

    ~HttpClient();

    // ── 简单 JSON POST（非流式）─────────────────────────────

    // 发送 POST JSON 请求，自动重试，返回解析后的 JSON 响应体。
    //
    // @param path API 路径（如 "/v1/chat/completions"），会自动拼接 base_url 中的路径前缀。
    // @param body 请求体 JSON。
    // @return API 返回的 JSON（HTTP 200 的 body）。
    // @throws std::runtime_error 网络错误、API 错误（非 200 且重试耗尽）或 JSON 解析错误。
    nlohmann::json post(const std::string& path,
                        const nlohmann::json& body);

    // ── 流式 POST（供 LLMClient::chat 使用）────────────────

    // 发送流式 POST 请求（通过 content_receiver 接收增量数据）。
    //
    // 与 post() 相同的指数退避重试策略（仅限可重试的网络错误/状态码）；
    // 不抛异常，失败以 httplib::Result 原样返回，错误处理交给调用方。
    //
    // @param path             API 路径，会自动拼接路径前缀。
    // @param body             请求体 JSON 字符串。
    // @param content_receiver 接收流式数据的回调，在 HTTP 接收线程中被多次调用；
    //                         返回 false 可中断接收（连接关闭，产生 Canceled 错误）。
    // @return HTTP 响应结果（调用方检查 res.error() 和 res->status）。
    httplib::Result postStreaming(
        const std::string& path,
        const std::string& body,
        std::function<bool(const char* data, size_t len)> content_receiver);

    // ── 查询 ──

    // 返回解析后的 host 部分（只读引用，生命周期与本对象一致）。
    const std::string& host() const { return host_; }

    // 返回 API Key（用于构造 Authorization 头；只读引用，生命周期与本对象一致）。
    const std::string& apiKey() const { return config_.api_key; }

    // 返回底层 httplib::Client（用于高级操作如连接池管理）。
    //
    // @return 底层客户端引用，由本对象拥有，调用方不得长期持有。
    httplib::Client& rawClient();

    // ── 静态工具函数（供调用方在 HttpClient 外部使用）──

    // 解析 API 错误响应 JSON，提取可读错误描述。
    //
    // @param http_status   HTTP 状态码（非 200）。
    // @param response_body 响应体原文（可能为 JSON 或任意文本）。
    // @return 人类可读的错误描述字符串。
    static std::string parseApiError(int http_status, const std::string& response_body);

    // 将 httplib::Error 枚举转换为中文错误描述。
    //
    // @param error_code httplib::Error 枚举值（以 int 传入）。
    // @return 对应的中文错误描述。
    static std::string httpErrorToString(int error_code);

    // 判断 HTTP 状态码是否可重试（如 429 / 5xx）。
    static bool isRetryableStatus(int status);

    // 判断网络错误（httplib::Error）是否可重试。
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
