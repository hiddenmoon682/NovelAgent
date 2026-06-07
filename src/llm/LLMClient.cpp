#include "llm/LLMClient.h"

#include "llm/SSEParser.h"
#include "llm/StreamAccumulator.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace llm {

// ===========================================================================
// 内部常量
// ===========================================================================

namespace {

// 超时配置（秒）
constexpr int kConnectionTimeout = 60;
constexpr int kReadTimeout = 180;   // 流式模式需较长超时（大模型逐 token 生成）
constexpr int kWriteTimeout = 60;

constexpr const char* kUserAgent = "NovelAgent/0.2.0";

/// 创建并配置 httplib::Client（避免在头文件中暴露 httplib 类型）
httplib::Client createHttpClient(const std::string& base_url)
{
    // base_url 格式："https://api.deepseek.com"
    std::string host = base_url;
    while (!host.empty() && host.back() == '/') {
        host.pop_back();
    }

    httplib::Client cli(host);
    cli.set_connection_timeout(kConnectionTimeout, 0);
    cli.set_read_timeout(kReadTimeout, 0);
    cli.set_write_timeout(kWriteTimeout, 0);
    return cli;
}

} // namespace

// ===========================================================================
// 构造 / 配置校验
// ===========================================================================

LLMClient::LLMClient(const ProviderConfig& config)
    : config_(config)
{
}

void LLMClient::validateConfig() const
{
    if (config_.api_key.empty()) {
        throw std::runtime_error("API Key 未配置，请设置环境变量或 config.json");
    }
    if (config_.base_url.empty()) {
        throw std::runtime_error("LLM base_url 未配置");
    }
    if (config_.model.empty()) {
        throw std::runtime_error("LLM model 未配置");
    }
}

// ===========================================================================
// buildRequestBody — 构造 API 请求体 JSON
// ===========================================================================

nlohmann::json LLMClient::buildRequestBody(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& system_prompt,
    bool stream) const
{
    nlohmann::json body;

    body["model"] = config_.model;

    // 消息列表 — system_prompt 作为首条 system 消息
    nlohmann::json msgs = nlohmann::json::array();

    if (!system_prompt.empty()) {
        msgs.push_back({
            {"role", "system"},
            {"content", system_prompt}
        });
    }

    for (const auto& msg : messages) {
        msgs.push_back(msg); // ADL → llm::to_json
    }

    body["messages"] = msgs;

    if (!tools.empty()) {
        body["tools"] = tools; // ADL → llm::to_json(ToolDefinition)
    }

    body["temperature"] = config_.temperature;
    body["max_tokens"] = config_.max_tokens;
    body["stream"] = stream;

    return body;
}

// ===========================================================================
// chatNonStreaming — 非流式调用
// ===========================================================================

LLMResponse LLMClient::chatNonStreaming(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& system_prompt)
{
    validateConfig();

    auto body = buildRequestBody(messages, tools, system_prompt, false);
    auto cli = createHttpClient(config_.base_url);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + config_.api_key},
        {"Content-Type", "application/json"},
        {"User-Agent", kUserAgent}
    };

    spdlog::info("[LLMClient] 非流式请求 → {} model={}", config_.base_url, config_.model);

    auto res = cli.Post("/v1/chat/completions", headers, body.dump(), "application/json");

    // 传输层错误
    if (!res) {
        auto err = httpErrorToString(static_cast<int>(res.error()));
        last_error_ = err;
        spdlog::error("[LLMClient] 网络错误: {}", err);
        throw std::runtime_error("LLM 请求失败: " + err);
    }

    // HTTP 错误
    if (res->status != 200) {
        auto err = parseApiError(res->status, res->body);
        last_error_ = err;
        spdlog::error("[LLMClient] API 错误 ({}): {}", res->status, err);
        throw std::runtime_error("API 错误: " + err);
    }

    // 解析 JSON 响应 → LLMResponse
    try {
        auto j = nlohmann::json::parse(res->body);
        return j.get<LLMResponse>();
    } catch (const nlohmann::json::exception& e) {
        last_error_ = e.what();
        spdlog::error("[LLMClient] JSON 解析失败: {}", e.what());
        throw std::runtime_error("API 响应解析失败: " + std::string(e.what()));
    }
}

// ===========================================================================
// chat — 流式调用
// ===========================================================================

LLMResponse LLMClient::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& system_prompt,
    StreamCallbacks callbacks)
{
    validateConfig();

    auto body = buildRequestBody(messages, tools, system_prompt, true);
    auto cli = createHttpClient(config_.base_url);

    spdlog::info("[LLMClient] 流式请求 → {} model={}", config_.base_url, config_.model);

    // ── 组装流式管道 ──
    SSEParser parser;
    StreamAccumulator acc;

    LLMResponse accumulated;
    bool stream_completed = false;
    bool tool_call_seen = false;
    std::string stream_error;
    std::string raw_response_body;

    // SSEParser → StreamAccumulator + 回调转发
    parser.setOnChunk([&](const StreamChunk& chunk) {
        acc.feed(chunk);

        if (callbacks.on_content && !chunk.content_delta.empty()) {
            callbacks.on_content(chunk.content_delta);
        }
        if (callbacks.on_reasoning && !chunk.reasoning_delta.empty()) {
            callbacks.on_reasoning(chunk.reasoning_delta);
        }
        if (callbacks.on_tool_call_start && !tool_call_seen
            && !chunk.tool_call_deltas.empty()) {
            tool_call_seen = true;
            callbacks.on_tool_call_start();
        }
    });

    parser.setOnError([&](const std::string& err) {
        stream_error = err;
        if (callbacks.on_error) {
            callbacks.on_error(err);
        }
    });

    acc.setOnDone([&](const LLMResponse& response) {
        accumulated = response;
        stream_completed = true;
        if (callbacks.on_complete) {
            callbacks.on_complete(response);
        }
    });

    // ── 发送流式 POST（通过 Request.content_receiver 接收流式数据）──
    httplib::Request req;
    req.method = "POST";
    req.path = "/v1/chat/completions";
    req.set_header("Authorization", "Bearer " + config_.api_key);
    req.set_header("Content-Type", "application/json");
    req.set_header("User-Agent", kUserAgent);
    req.body = body.dump();
    req.content_receiver = [&](const char* data, size_t len, uint64_t /*offset*/,
                                uint64_t /*total*/) {
        raw_response_body.append(data, len);
        parser.feed(std::string(data, len));
        return true;
    };

    auto res = cli.send(req);

    // ── 传输层错误 ──
    if (!res) {
        auto err = httpErrorToString(static_cast<int>(res.error()));
        last_error_ = err;
        spdlog::error("[LLMClient] 网络错误: {}", err);
        if (callbacks.on_error) callbacks.on_error(err);
        throw std::runtime_error("LLM 请求失败: " + err);
    }

    // ── HTTP 错误 ──
    if (res->status != 200) {
        auto err = parseApiError(res->status, raw_response_body);
        last_error_ = err;
        spdlog::error("[LLMClient] API 错误 ({}): {}", res->status, err);
        if (callbacks.on_error) callbacks.on_error(err);
        throw std::runtime_error("API 错误: " + err);
    }

    // ── SSE 解析错误 ──
    if (!stream_error.empty()) {
        last_error_ = stream_error;
        throw std::runtime_error("SSE 解析错误: " + stream_error);
    }

    // ── 流未正常结束 ──
    if (!stream_completed) {
        last_error_ = "流式响应未正常结束（未收到 finish_reason）";
        spdlog::error("[LLMClient] {}", last_error_);
        if (callbacks.on_error) callbacks.on_error(last_error_);
        throw std::runtime_error(last_error_);
    }

    return accumulated;
}

// ===========================================================================
// 错误处理辅助
// ===========================================================================

std::string LLMClient::parseApiError(int http_status, const std::string& response_body) const
{
    // 优先解析 JSON 错误体（OpenAI/DeepSeek 格式: {"error": {"message": "...", "code": "..."}}）
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

    // 回退：HTTP 状态码描述
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

std::string LLMClient::httpErrorToString(int error_code)
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

} // namespace llm
