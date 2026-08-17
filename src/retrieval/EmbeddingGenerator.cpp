// EmbeddingGenerator 实现 — 委托 HttpClient 发送 HTTP 请求。

#include "retrieval/EmbeddingGenerator.h"

#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

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

    llm::HttpConfig http_cfg;
    http_cfg.base_url = provider_config_.base_url;
    http_cfg.api_key = provider_config_.api_key;
    http_cfg.connect_timeout = 30;
    http_cfg.read_timeout = 60;
    http_cfg.max_retries = 3;
    http_ = std::make_unique<llm::HttpClient>(http_cfg);
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

    llm::HttpConfig http_cfg;
    http_cfg.base_url = provider_config_.base_url;
    http_cfg.api_key = provider_config_.api_key;
    http_cfg.connect_timeout = 30;
    http_cfg.read_timeout = 60;
    http_cfg.max_retries = 3;
    http_ = std::make_unique<llm::HttpClient>(http_cfg);
}

// ===========================================================================
// 公开接口
// ===========================================================================

std::vector<float> EmbeddingGenerator::generateEmbedding(const std::string& text)
{
    // 不在此加锁：委托 generateEmbeddings（其内部已持 mutex_），嵌套加锁会死锁。
    auto results = generateEmbeddings({text});
    if (results.empty()) {
        throw std::runtime_error("[EmbeddingGenerator] 嵌入生成返回空结果");
    }
    return results[0];
}

std::vector<std::vector<float>> EmbeddingGenerator::generateEmbeddings(
    const std::vector<std::string>& texts)
{
    // 串行化并发调用：维度写入与共享 http_ 请求在同一把锁内，避免 dimension_
    // 数据竞争与 httplib::Client 并发 post 的未定义行为（网络密集，等待可接受）。
    std::lock_guard<std::mutex> lock(mutex_);

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

        spdlog::debug("[EmbeddingGenerator] 发送批量嵌入请求: {} 条文本", batch.size());

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

int EmbeddingGenerator::dimension() const {
    // 与 generateEmbeddings 内的维度写入互斥，避免跨线程撕裂读。
    std::lock_guard<std::mutex> lock(mutex_);
    return dimension_;
}

// ===========================================================================
// HTTP 请求（委托 HttpClient::post）
// ===========================================================================

json EmbeddingGenerator::sendEmbeddingRequest(
    const std::vector<std::string>& texts) const
{
    json request_body;
    request_body["model"] = embed_config_.model;
    request_body["input"] = texts;

    // 委托 HttpClient 处理 URL/认证/重试/错误
    return http_->post("/v1/embeddings", request_body);
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

    // 截断到 max_text_length 字符，优先在句子边界处截断
    std::string truncated = text.substr(0, embed_config_.max_text_length);

    // A11: UTF-8 安全回退——避免在多字节字符中间截断产生乱码
    while (!truncated.empty() &&
           (static_cast<unsigned char>(truncated.back()) & 0xC0) == 0x80) {
        truncated.pop_back();
    }

    auto last_period = truncated.find_last_of(".。！？!?");
    if (last_period != std::string::npos
        && static_cast<int>(last_period) > embed_config_.max_text_length / 2) {
        truncated = truncated.substr(0, last_period + 1);
    }

    return truncated;
}

} // namespace retrieval
