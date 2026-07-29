// HttpClient 实现 — 共享的 HTTP 基础设施。

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
    // 构造完整 scheme+host，httplib 需要 "https://host" 格式才能启用 SSL
    client_ = std::make_unique<httplib::Client>(scheme_ + "://" + host_);
    client_->set_follow_location(true);
    client_->set_connection_timeout(config_.connect_timeout, 0);
    client_->set_read_timeout(config_.read_timeout, 0);
    client_->set_keep_alive(true);

    spdlog::debug("[HttpClient] 初始化 → {}://{} (prefix={})",
                  scheme_, host_, path_prefix_);
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

// ===========================================================================
// fullPath — 将 path_prefix_ 与具体 API 路径拼接为完整请求路径
// 例如: path_prefix_="/v1", path="/chat/completions" → "/v1/chat/completions"
// ===========================================================================

std::string HttpClient::fullPath(const std::string& path) const
{
    return path_prefix_ + path;
}

// ===========================================================================
// defaultHeaders — 构造每个 HTTP 请求都携带的默认请求头
//
// 包含:
//   Authorization — Bearer Token 认证，使用配置中的 API Key
//   Content-Type — 固定 application/json（所有请求体均为 JSON）
//   User-Agent  — 客户端标识，方便服务端识别调用来源
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
// post — 非流式 JSON POST（发送完整 JSON 请求体，等待完整 JSON 响应）
//
// 参数:
//   path — API 相对路径，与 path_prefix_ 拼接为完整路径
//   body — 请求体，自动序列化为 JSON 字符串
//
// 重试策略（指数退避）:
//   可重试的网络错误（Connection/Read/ConnectionTimeout）
//   可重试的 HTTP 状态码（429/502/503）
//   → 每次重试间隔翻倍，最多重试 config_.max_retries 次
//
// 不可重试的情况（直接抛异常）:
//   非可重试的网络错误（SSL、代理等）
//   非可重试的 HTTP 状态码（400/401/403/404/500 等）
//   JSON 响应体解析失败
//
// 返回:
//   解析后的 JSON 响应体（已确保 HTTP 200）
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

        // 发送 HTTP POST 请求（httplib 封装）
        //   path_full — 完整 API 路径
        //   headers   — 请求头（Authorization/Content-Type/User-Agent）
        //   body_str  — JSON 序列化后的请求体
        //   "application/json" — 固定内容类型，服务端根据此解析请求体
        //   （与 headers 中的重复，但 httplib 要求传此参数）
        
        //   返回值 res — httplib::Result：
        //     !res 为 true  → 传输层错误，res.error() 获取错误码
        //     res->status   → HTTP 状态码
        //     res->body     → 响应体字符串
        auto res = client_->Post(path_full, headers, body_str, "application/json");

        // ── 传输层错误（连接断开、超时等） ──
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

        // ── HTTP 错误（服务端返回了非 200 状态码） ──
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

    // 所有重试次数耗尽仍未成功
    throw std::runtime_error("HTTP 请求失败: 超过最大重试次数");
}

// ===========================================================================
// postStreaming — 流式 POST（用于 SSE / Chat Completions 流式响应）
//
// 与 post() 的关键区别:
//   1. body 为预序列化的 std::string（由调用方自行组装）
//   2. 不等待完整响应，而是通过 content_receiver 逐块接收流式数据
//   3. 返回 httplib::Result 而非 nlohmann::json，由调用方处理响应
//   4. 手动构造 httplib::Request（设置 content_receiver 回调）
//
// 参数:
//   path              — API 相对路径
//   body              — 预序列化的 JSON 请求体字符串
//   content_receiver  — 数据块回调，每次收到数据时被调用
//                      返回 true=继续接收，false=取消接收
//
// 重试策略:
//   与 post() 相同的指数退避，但 max_attempts 取自 config_.max_retries，
//   若未配置则默认 2（共 3 次尝试）；且仅限尚未向下游交付过任何
//   字节时才重试，避免已消费的增量被重复喂给下游
//
// 返回:
//   httplib::Result — 由调用方判断成功/失败并解析
// ===========================================================================

httplib::Result HttpClient::postStreaming(
    const std::string& path,
    const std::string& body,
    std::function<bool(const char* data, size_t len)> content_receiver)
{
    std::string path_full = fullPath(path);
    auto headers = defaultHeaders();

    const int max_attempts = config_.max_retries > 0 ? config_.max_retries : 2;
    int retry_delay_ms = config_.retry_base_delay_ms;

    for (int attempt = 0; attempt <= max_attempts; ++attempt) {
        if (attempt > 0) {
            spdlog::warn("[HttpClient] 流式重试 {}/{} ({}ms 后)",
                         attempt, max_attempts, retry_delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }

        // ── 手动构造 httplib::Request ──
        // post() 使用便捷方法 client_->Post(...)，但流式请求需要
        // 手动设置 content_receiver，所以改用 client_->send(req)
        httplib::Request req;
        req.method = "POST";
        req.path = path_full;
        for (const auto& [key, val] : headers) {
            req.set_header(key.c_str(), val.c_str());
        }
        req.body = body;

        // ── 设置流式数据接收回调 ──
        // httplib 的 content_receiver 签名:
        //   bool(const char* data, size_t len, uint64_t offset, uint64_t total)
        // 外层捕获 content_receiver，每次重试重新构造 lambda，
        // 避免因移动导致回调失效（use-after-move 防护）
        //
        // WHY：delivered 记录本次尝试是否已向下游交付过字节。一旦交付过
        // （如 SSE 增量已回调给调用方）再整体重试，会把相同增量重复喂给
        // 下游（内容重复显示/重复累积），此时必须直接报错返回而非重试。
        bool delivered = false;
        req.content_receiver = [receiver = content_receiver, &delivered](
            const char* data, size_t len, uint64_t /*offset*/, uint64_t /*total*/) {
            if (len > 0) delivered = true;
            return receiver(data, len);
        };

        spdlog::debug("[HttpClient] 流式 POST {} (attempt {})", path_full, attempt);
        auto res = client_->send(req);

        // ── 传输层错误 ──
        if (!res) {
            int err_code = static_cast<int>(res.error());
            // WHY：已交付过字节则不再重试（见上方 delivered 说明）
            if (!delivered && attempt < max_attempts && isRetryableNetworkError(err_code)) {
                retry_delay_ms *= 2;
                continue;
            }
            if (delivered) {
                spdlog::warn("[HttpClient] 流式传输中断但部分数据已交付，不重试以避免增量重复");
            }
            // 不可重试 → 将原始 Result 返回给调用方，由调用方决定如何处理
            return res;
        }

        // ── HTTP 错误 ──
        if (res->status != 200) {
            // WHY：非 200 的响应体字节也会经 content_receiver 交付给下游，
            // 此时重试同样会污染下游状态（错误体已被消费），故一并受
            // delivered 约束。
            if (!delivered && attempt < max_attempts && isRetryableStatus(res->status)) {
                retry_delay_ms *= 2;
                continue;
            }
            return res;
        }

        // ── 成功 ──
        return res;
    }

    // 不应到达此处（循环内必 return）
    throw std::runtime_error("流式请求失败: 超过最大重试次数");
}

// ===========================================================================
// rawClient — 返回底层 httplib::Client 的引用
//
// 当调用方需要执行 post()/postStreaming() 之外的特殊 HTTP 操作时
// （例如 GET、PUT、DELETE、自定义超时等），可通过此方法直接操作
// 原始的 httplib::Client。
//
// 注意: 尽量避免使用此方法，优先使用 HttpClient 封装的方法，
// 以确保重试、错误处理等逻辑的一致性。
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
    // ── 尝试从响应体中提取 JSON 格式的错误信息 ──
    // LLM API（OpenAI / DeepSeek 等）的错误响应格式通常为:
    //   {"error": {"message": "...", "code": "..."}}
    // 成功解析后优先返回 "[code] message" 格式，其次是纯 message。
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
            // catch (...) — 如果响应体不是合法 JSON（例如纯文本错误），捕获所有异常，然后静默忽略
            // JSON 解析失败 → 忽略，回退到下面的 HTTP 状态码描述
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
        case 302:
        case 301: return "API 地址已变更，请检查 base_url 配置 (HTTP " + std::to_string(http_status) + ")";
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

// ===========================================================================
// isRetryableStatus — 判断 HTTP 状态码是否值得重试
//
// 429 Too Many Requests     — 触发限流，等待后重试通常可恢复
// 502 Bad Gateway           — 上游临时故障，通常几秒后自动恢复
// 503 Service Unavailable   — 服务过载/维护中，等待后可能恢复
//
// 其他错误（400/401/403/404/500 等）直接放弃，无需重试。
// ===========================================================================

bool HttpClient::isRetryableStatus(int status)
{
    return status == 429 || status == 502 || status == 503;
}

// ===========================================================================
// isRetryableNetworkError — 判断传输层错误是否值得重试
//
// httplib::Error::Connection         — 连接失败（服务器未响应）
// httplib::Error::Read               — 读取超时或连接中途断开
// httplib::Error::ConnectionTimeout  — 建立连接超时
//
// 以上错误均为网络层面临时性问题，重试通常可以恢复。
// SSL、代理、证书验证等错误直接放弃，无需重试。
// ===========================================================================

bool HttpClient::isRetryableNetworkError(int err)
{
    auto e = static_cast<httplib::Error>(err);
    return e == httplib::Error::Connection
        || e == httplib::Error::Read
        || e == httplib::Error::ConnectionTimeout;
}

} // namespace llm
