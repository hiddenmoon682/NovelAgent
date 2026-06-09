/// EmbeddingGenerator 实现 — HTTP POST 调用 OpenAI 兼容 embeddings API。

#include "retrieval/EmbeddingGenerator.h"

#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace retrieval {

// ===========================================================================
// 构造
// ===========================================================================

EmbeddingGenerator::EmbeddingGenerator(const ProviderConfig& provider_config)
    : provider_config_(provider_config)
{
    if (provider_config_.api_key.empty()) {
        spdlog::warn("[EmbeddingGenerator] API Key 为空，嵌入生成将不可用");
    }
}

EmbeddingGenerator::EmbeddingGenerator(
    const ProviderConfig& provider_config,
    const EmbeddingConfig& embed_config)
    : provider_config_(provider_config)
    , embed_config_(embed_config)
{
    if (provider_config_.api_key.empty()) {
        spdlog::warn("[EmbeddingGenerator] API Key 为空，嵌入生成将不可用");
    }
}

// ===========================================================================
// 公开接口
// ===========================================================================

std::vector<float> EmbeddingGenerator::generateEmbedding(const std::string& text)
{
    auto results = generateEmbeddings({text});
    if (results.empty()) {
        throw std::runtime_error("[EmbeddingGenerator] 嵌入生成返回空结果");
    }
    return results[0];
}

std::vector<std::vector<float>> EmbeddingGenerator::generateEmbeddings(
    const std::vector<std::string>& texts)
{
    if (texts.empty()) {
        return {};
    }

    if (provider_config_.api_key.empty()) {
        throw std::runtime_error("[EmbeddingGenerator] API Key 未配置，无法生成嵌入向量");
    }

    std::vector<std::vector<float>> all_embeddings;

    // 按 max_batch_size 分批
    for (size_t offset = 0; offset < texts.size();
         offset += embed_config_.max_batch_size) {
        size_t batch_size = std::min(
            static_cast<size_t>(embed_config_.max_batch_size),
            texts.size() - offset);

        std::vector<std::string> batch(
            texts.begin() + offset,
            texts.begin() + offset + batch_size);

        // 预处理：截断超长文本
        for (auto& text : batch) {
            text = preprocessText(text);
        }

        spdlog::debug("[EmbeddingGenerator] 发送批量嵌入请求: {} 条文本 (offset={})",
                      batch.size(), offset);

        try {
            json response = sendEmbeddingRequest(batch);
            auto embeddings = parseEmbeddingsResponse(response);

            if (!dimension_ && !embeddings.empty()) {
                dimension_ = static_cast<int>(embeddings[0].size());
                spdlog::info("[EmbeddingGenerator] 嵌入维度: {}", dimension_);
            }

            all_embeddings.insert(all_embeddings.end(),
                                  std::make_move_iterator(embeddings.begin()),
                                  std::make_move_iterator(embeddings.end()));
        } catch (const std::exception& e) {
            spdlog::error("[EmbeddingGenerator] 批量嵌入请求失败: {}", e.what());
            throw;
        }
    }

    return all_embeddings;
}

// ===========================================================================
// HTTP 请求
// ===========================================================================

json EmbeddingGenerator::sendEmbeddingRequest(
    const std::vector<std::string>& texts) const
{
    // 解析 base_url 获取 host 和 path
    std::string url = provider_config_.base_url;
    // 移除末尾斜杠
    while (!url.empty() && url.back() == '/') url.pop_back();

    // 提取 scheme + host
    std::string scheme = "https";
    std::string host;
    std::string path_prefix;

    if (url.find("https://") == 0) {
        url = url.substr(8);
    } else if (url.find("http://") == 0) {
        scheme = "http";
        url = url.substr(7);
    }

    size_t slash_pos = url.find('/');
    if (slash_pos != std::string::npos) {
        host = url.substr(0, slash_pos);
        path_prefix = url.substr(slash_pos);
    } else {
        host = url;
    }

    // 构造请求体
    json request_body;
    request_body["model"] = embed_config_.model;
    request_body["input"] = texts;

    // 发送 HTTP POST
    httplib::Client cli(host);
    cli.set_connection_timeout(30);
    cli.set_read_timeout(60);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + provider_config_.api_key},
        {"Content-Type", "application/json"}
    };

    std::string api_path = path_prefix + "/v1/embeddings";

    // 指数退避重试（最多 3 次）
    const int max_retries = 3;
    int retry_delay_ms = 1000;

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        auto res = cli.Post(api_path, headers,
                           request_body.dump(), "application/json");

        if (res) {
            if (res->status == 200) {
                return json::parse(res->body);
            }

            // 可重试的错误码
            if (res->status == 429 || res->status == 502 || res->status == 503) {
                if (attempt < max_retries - 1) {
                    spdlog::warn("[EmbeddingGenerator] HTTP {} — 重试 {}/{} ({}ms)",
                                 res->status, attempt + 1, max_retries, retry_delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                    retry_delay_ms *= 2;
                    continue;
                }
            }

            // 不可重试的错误
            std::string error_msg = "HTTP " + std::to_string(res->status);
            try {
                auto err_json = json::parse(res->body);
                if (err_json.contains("error") && err_json["error"].contains("message")) {
                    error_msg += ": " + err_json["error"]["message"].get<std::string>();
                }
            } catch (...) {}

            throw std::runtime_error("[EmbeddingGenerator] API 错误: " + error_msg);
        } else {
            // 网络错误
            auto err = res.error();
            if (attempt < max_retries - 1) {
                spdlog::warn("[EmbeddingGenerator] 网络错误 — 重试 {}/{} ({}ms)",
                             attempt + 1, max_retries, retry_delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                retry_delay_ms *= 2;
                continue;
            }
            throw std::runtime_error(
                "[EmbeddingGenerator] 网络错误: " + httplib::to_string(err));
        }
    }

    throw std::runtime_error("[EmbeddingGenerator] 请求失败：已达最大重试次数");
}

// ===========================================================================
// 响应解析
// ===========================================================================

std::vector<std::vector<float>> EmbeddingGenerator::parseEmbeddingsResponse(
    const json& response)
{
    std::vector<std::vector<float>> results;

    if (!response.contains("data") || !response["data"].is_array()) {
        throw std::runtime_error("[EmbeddingGenerator] 响应中缺少 data 字段");
    }

    for (const auto& item : response["data"]) {
        if (!item.contains("embedding") || !item["embedding"].is_array()) {
            spdlog::warn("[EmbeddingGenerator] 响应条目中缺少 embedding 字段");
            continue;
        }

        std::vector<float> embedding;
        for (const auto& val : item["embedding"]) {
            if (val.is_number()) {
                embedding.push_back(val.get<float>());
            }
        }
        results.push_back(std::move(embedding));
    }

    return results;
}

// ===========================================================================
// 文本预处理
// ===========================================================================

std::string EmbeddingGenerator::preprocessText(const std::string& text) const
{
    if (static_cast<int>(text.size()) <= embed_config_.max_text_length) {
        return text;
    }

    // 截断到 max_text_length 字符
    // 优先在句子边界处截断
    std::string truncated = text.substr(0, embed_config_.max_text_length);

    // 查找最后一个句子结束标记
    auto last_period = truncated.find_last_of(".。！？!?");
    if (last_period != std::string::npos && static_cast<int>(last_period) > embed_config_.max_text_length / 2) {
        truncated = truncated.substr(0, last_period + 1);
    }

    return truncated;
}

} // namespace retrieval
