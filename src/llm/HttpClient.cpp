/// HttpClient 实现 — 共享的 HTTP 基础设施。

#include "llm/HttpClient.h"

#include <spdlog/spdlog.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <thread>
#include <chrono>

namespace llm {

// ===========================================================================
// 构造 / 析构
// ===========================================================================

HttpClient::HttpClient(const HttpConfig& config)
    : config_(config)
{
    parseUrl();
    client_ = std::make_unique<httplib::Client>(host_);
    client_->set_connection_timeout(config_.connect_timeout, 0);
    client_->set_read_timeout(config_.read_timeout, 0);
    client_->set_keep_alive(true);

    spdlog::debug("[HttpClient] 初始化 → {} (host={}, prefix={})",
                  config_.base_url, host_, path_prefix_);
}

HttpClient::~HttpClient() = default;

// ===========================================================================
// URL 解析
// ===========================================================================

void HttpClient::parseUrl()
{
    std::string url = config_.base_url;
    while (!url.empty() && url.back() == '/') url.pop_back();

    scheme_ = "https";
    if (url.find("https://") == 0) {
        url = url.substr(8);
    } else if (url.find("http://") == 0) {
        scheme_ = "http";
        url = url.substr(7);
    }

    size_t slash_pos = url.find('/');
    if (slash_pos != std::string::npos) {
        host_ = url.substr(0, slash_pos);
        path_prefix_ = url.substr(slash_pos);
    } else {
        host_ = url;
    }
}

std::string HttpClient::fullPath(const std::string& path) const
{
    return path_prefix_ + path;
}

// ===========================================================================
// 请求头
// ===========================================================================

httplib::Headers HttpClient::defaultHeaders() const
{
    return {
        {"Authorization", "Bearer " + config_.api_key},
        {"Content-Type", "application/json"},
        {"User-Agent", "NovelAgent/0.3.0"}
    };
}

// ===========================================================================
// post — 简单 JSON POST（非流式，自动重试）
// ===========================================================================

nlohmann::json HttpClient::post(
    const std::string& path,
    const nlohmann::json& body)
{
    std::string path_full = fullPath(path);
    std::string body_str = body.dump();
    auto headers = defaultHeaders();

    spdlog::debug("[HttpClient] POST {} (body {} bytes)", path_full, body_str.size());

    int retry_delay_ms = config_.retry_base_delay_ms;

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
        if (attempt > 0) {
            spdlog::warn("[HttpClient] 重试 {}/{} ({}ms 后)",
                         attempt, config_.max_retries, retry_delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }

        auto res = client_->Post(path_full, headers, body_str, "application/json");

        // ── 传输层错误 ──
        if (!res) {
            int err_code = static_cast<int>(res.error());
            if (attempt < config_.max_retries && isRetryableNetworkError(err_code)) {
                retry_delay_ms *= 2;
                continue;
            }
            auto err = httpErrorToString(err_code);
            spdlog::error("[HttpClient] 网络错误: {}", err);
            throw std::runtime_error("HTTP 请求失败: " + err);
        }

        // ── HTTP 错误 ──
        if (res->status != 200) {
            if (attempt < config_.max_retries && isRetryableStatus(res->status)) {
                spdlog::warn("[HttpClient] HTTP {} 可重试", res->status);
                retry_delay_ms *= 2;
                continue;
            }
            auto err = parseApiError(res->status, res->body);
            spdlog::error("[HttpClient] API 错误 ({}): {}", res->status, err);
            throw std::runtime_error("API 错误: " + err);
        }

        // ── 成功 → 解析 JSON ──
        try {
            return nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("[HttpClient] JSON 解析失败: {}", e.what());
            throw std::runtime_error(
                "API 响应解析失败: " + std::string(e.what()));
        }
    }

    throw std::runtime_error("HTTP 请求失败: 超过最大重试次数");
}

// ===========================================================================
// postStreaming — 流式 POST
// ===========================================================================

httplib::Result HttpClient::postStreaming(
    const std::string& path,
    const std::string& body,
    std::function<bool(const char* data, size_t len)> content_receiver)
{
    std::string path_full = fullPath(path);
    auto headers = defaultHeaders();

    httplib::Request req;
    req.method = "POST";
    req.path = path_full;
    for (const auto& [key, val] : headers) {
        req.set_header(key.c_str(), val.c_str());
    }
    req.body = body;

    // 包装 content_receiver 以匹配 httplib 的签名
    req.content_receiver = [receiver = std::move(content_receiver)](
        const char* data, size_t len, uint64_t /*offset*/, uint64_t /*total*/) {
        return receiver(data, len);
    };

    spdlog::debug("[HttpClient] 流式 POST {}", path_full);
    return client_->send(req);
}

// ===========================================================================
// 查询
// ===========================================================================

httplib::Client& HttpClient::rawClient()
{
    return *client_;
}

// ===========================================================================
// 错误处理（静态工具函数）
// ===========================================================================

std::string HttpClient::parseApiError(int http_status, const std::string& response_body)
{
    if (!response_body.empty()) {
        try {
            auto j = nlohmann::json::parse(response_body);
            if (j.contains("error") && j["error"].is_object()) {
                auto& err = j["error"];
                std::string msg = err.value("message", "");
                std::string code = err.value("code", "");
                if (!code.empty()) {
                    return "[" + code + "] " + msg;
                }
                if (!msg.empty()) {
                    return msg;
                }
            }
        } catch (...) {
            // JSON 解析失败，回退到状态码描述
        }
    }

    switch (http_status) {
        case 400: return "请求参数错误 (400)";
        case 401: return "API Key 无效或未授权 (401)";
        case 403: return "权限不足 (403)";
        case 404: return "API 端点不存在 (404)";
        case 429: return "请求频率过高，请稍后再试 (429)";
        case 500: return "LLM 服务端内部错误 (500)";
        case 502: return "LLM 服务暂时不可用 (502)";
        case 503: return "LLM 服务正在维护中 (503)";
        default:  return "HTTP " + std::to_string(http_status);
    }
}

std::string HttpClient::httpErrorToString(int error_code)
{
    auto err = static_cast<httplib::Error>(error_code);

    switch (err) {
        case httplib::Error::Success:              return "成功";
        case httplib::Error::Unknown:              return "未知网络错误";
        case httplib::Error::Connection:           return "连接失败（服务器不可达）";
        case httplib::Error::BindIPAddress:        return "网络绑定失败";
        case httplib::Error::Read:                 return "读取超时或连接断开";
        case httplib::Error::Write:                return "写入数据失败";
        case httplib::Error::ExceedRedirectCount:  return "重定向次数过多";
        case httplib::Error::Canceled:             return "请求已取消";
        case httplib::Error::SSLConnection:        return "SSL/TLS 连接错误";
        case httplib::Error::SSLLoadingCerts:      return "SSL 证书加载失败";
        case httplib::Error::SSLServerVerification: return "SSL 服务端证书验证失败";
        case httplib::Error::ConnectionTimeout:    return "连接超时";
        case httplib::Error::ProxyConnection:      return "代理连接失败";
        default:                                   return "网络错误 (code: " + std::to_string(error_code) + ")";
    }
}

bool HttpClient::isRetryableStatus(int status)
{
    return status == 429 || status == 502 || status == 503;
}

bool HttpClient::isRetryableNetworkError(int err)
{
    auto e = static_cast<httplib::Error>(err);
    return e == httplib::Error::Connection
        || e == httplib::Error::Read
        || e == httplib::Error::ConnectionTimeout;
}

} // namespace llm
