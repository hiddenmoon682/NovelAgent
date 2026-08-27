#include "llm/LLMClient.h"

#include "llm/EmojiStripFilter.h"
#include "llm/StreamingPipeline.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
// postStreaming 返回 httplib::Result（HttpClient.h 仅前向声明），
// 消费返回值需要完整类型，故在实现文件中包含
#include <httplib.h>

#include <stdexcept>

namespace llm {

// ===========================================================================
// 构造 / 配置校验
// ===========================================================================

LLMClient::LLMClient(const ProviderConfig& config)
    : config_(config)
    , http_(HttpConfig{
          config.base_url,
          config.api_key,
          60,    // connect_timeout
          180,   // read_timeout
          3,     // max_retries
          1000   // retry_base_delay_ms
      })
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
        nlohmann::json sys_msg = {
            {"role", "system"},
            {"content", system_prompt}
        };
        // cache_control: system prompt 始终标记缓存断点
        if (config_.supports_cache_control) {
            sys_msg["content"] = nlohmann::json::array({
                {{"type", "text"}, {"text", system_prompt},
                 {"cache_control", {{"type", "ephemeral"}}}}
            });
        }
        msgs.push_back(std::move(sys_msg));
    }

    // 标记最后 2 条消息为缓存断点（仅当 provider 支持时）
    int cache_mark_count = config_.supports_cache_control ? 2 : 0;
    int cache_start = std::max(0, static_cast<int>(messages.size()) - cache_mark_count);
    for (int i = 0; i < static_cast<int>(messages.size()); ++i) {
        const auto& msg = messages[i];
        if (i >= cache_start && config_.supports_cache_control) {
            // 对带缓存标记的消息用 content 数组格式
            nlohmann::json cached_msg = msg;  // ADL → to_json
            std::string text = msg.content;
            cached_msg["content"] = nlohmann::json::array({
                {{"type", "text"}, {"text", text},
                 {"cache_control", {{"type", "ephemeral"}}}}
            });
            msgs.push_back(std::move(cached_msg));
        } else {
            msgs.push_back(msg);
        }
    }

    body["messages"] = msgs;

    if (!tools.empty()) {
        body["tools"] = tools; // ADL → llm::to_json(ToolDefinition)
    }

    body["temperature"] = config_.temperature;
    body["max_tokens"] = config_.max_tokens;
    if (config_.enable_thinking) {
        body["thinking"] = nlohmann::json{{"type", "enabled"}};
        body["reasoning_effort"] = config_.reasoning_effort;
    }
    body["stream"] = stream;

    return body;
}

// ===========================================================================
// chatNonStreaming — 非流式调用（委托 HttpClient::post）
// ===========================================================================

LLMResponse LLMClient::chatNonStreaming(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& system_prompt)
{
    validateConfig();

    auto body = buildRequestBody(messages, tools, system_prompt, false);

    spdlog::info("[LLMClient] 非流式请求 → {} model={}", config_.base_url, config_.model);

    try {
        auto j = http_.post("/v1/chat/completions", body);
        LLMResponse r = j.get<LLMResponse>();
        // 与流式路径对齐：内容与推理文本剥离 emoji（工具参数不处理 ——
        // 它们是结构化 JSON，且可能承载用户的创作内容）。
        r.content = EmojiStripFilter::stripOnce(r.content);
        r.reasoning_content = EmojiStripFilter::stripOnce(r.reasoning_content);
        return r;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        throw;
    }
}

// ===========================================================================
// chat — 流式调用（使用 HttpClient::postStreaming）
// ===========================================================================

LLMResponse LLMClient::chat(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& system_prompt,
    StreamCallbacks callbacks,
    const std::atomic<bool>* cancel_flag)
{
    validateConfig();

    auto body = buildRequestBody(messages, tools, system_prompt, true);

    spdlog::info("[LLMClient] 流式请求 → {} model={}", config_.base_url, config_.model);

    // ── 组装流式管道 ──
    StreamingPipeline pipeline;
    pipeline.setCallbacks(callbacks);

    // ── 流式 POST ──
    std::string raw_response_body;

    auto res = http_.postStreaming(
        "/v1/chat/completions",
        body.dump(),
        [&](const char* data, size_t len) {
            // 检查取消标志：若用户取消则中止流式接收
            if (cancel_flag && *cancel_flag) {
                spdlog::warn("[LLMClient] 收到取消信号，中断流式响应");
                return false;  // httplib 关闭连接，返回 Canceled 错误
            }
            raw_response_body.append(data, len);
            pipeline.feed(std::string(data, len));
            return true;
        });

    // ── 传输层错误 ──
    if (!res) {
        // 取消导致的 Canceled 错误 → 返回已累积的部分响应
        if (cancel_flag && *cancel_flag) {
            spdlog::warn("[LLMClient] 用户取消，返回部分响应");
            return pipeline.response();  // 含已生成的部分文本，tool_calls 为空
        }
        auto err = HttpClient::httpErrorToString(static_cast<int>(res.error()));
        last_error_ = err;
        spdlog::error("[LLMClient] 网络错误: {}", err);
        if (callbacks.on_error) callbacks.on_error(err);
        throw std::runtime_error("LLM 请求失败: " + err);
    }

    // ── HTTP 错误 ──
    if (res->status != 200) {
        auto err = HttpClient::parseApiError(res->status, raw_response_body);
        last_error_ = err;
        spdlog::error("[LLMClient] API 错误 ({}): {}", res->status, err);
        if (callbacks.on_error) callbacks.on_error(err);
        throw std::runtime_error("API 错误: " + err);
    }

    // ── SSE 解析错误 ──
    if (pipeline.hasError()) {
        last_error_ = pipeline.error();
        throw std::runtime_error("SSE 解析错误: " + pipeline.error());
    }

    // ── 流未正常结束 ──
    if (!pipeline.completed()) {
        last_error_ = "流式响应未正常结束（未收到 finish_reason）";
        spdlog::error("[LLMClient] {}", last_error_);
        if (callbacks.on_error) callbacks.on_error(last_error_);
        throw std::runtime_error(last_error_);
    }

    return pipeline.response();
}

} // namespace llm
