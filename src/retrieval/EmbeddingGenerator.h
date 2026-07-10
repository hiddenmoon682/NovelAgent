#pragma once

// 嵌入向量生成器 — 调用 LLM API 的 embeddings endpoint。
//
// Phase 4 架构改进：通过组合 HttpClient 复用 HTTP 基础设施，
// 与 LLMClient 共享 URL 解析/认证/重试逻辑。
//
// 支持的嵌入模型（OpenAI 兼容）:
//   - DeepSeek: text-embedding-3-small (默认)
//   - Kimi: 通过 /v1/embeddings endpoint

#include "config/AppConfig.h"
#include "llm/HttpClient.h"
#include "retrieval/IEmbeddingGenerator.h"

#include <memory>
#include <string>
#include <vector>

namespace retrieval {

// 嵌入生成请求配置。
struct EmbeddingConfig {
    std::string model = "text-embedding-3-small";  // 嵌入模型名称
    int max_batch_size = 100;                       // 单次 API 调用最大文本数
    int max_text_length = 8000;                     // 单条文本最大字符数（超长截断）
};

// 嵌入向量生成器 — OpenAI /v1/embeddings API 实现。
//
// 线程安全：不安全。同一实例不应并发调用。
class EmbeddingGenerator : public IEmbeddingGenerator {
public:
    // 构造函数，接收 ProviderConfig（复用 base_url + api_key）。
    explicit EmbeddingGenerator(const ProviderConfig& provider_config);

    // 带嵌入配置的构造函数。
    EmbeddingGenerator(const ProviderConfig& provider_config,
                       const EmbeddingConfig& embed_config);

    ~EmbeddingGenerator() override = default;

    // ── IEmbeddingGenerator 接口实现 ──

    std::vector<float> generateEmbedding(const std::string& text) override;
    std::vector<std::vector<float>> generateEmbeddings(
        const std::vector<std::string>& texts) override;

    // 返回嵌入向量的维度（首次调用后确定）。
    int dimension() const override { return dimension_; }

    // 返回当前嵌入配置。
    const EmbeddingConfig& embedConfig() const { return embed_config_; }

private:
    ProviderConfig provider_config_;
    EmbeddingConfig embed_config_;
    int dimension_ = 0;
    std::unique_ptr<llm::HttpClient> http_;  // Phase 4 改进：复用共享 HTTP 基础设施

    // 发送 POST /v1/embeddings 请求（委托 HttpClient::post）。
    nlohmann::json sendEmbeddingRequest(
        const std::vector<std::string>& texts) const;

    // 从 API 响应的 JSON 中提取嵌入向量列表。
    static std::vector<std::vector<float>> parseEmbeddingsResponse(
        const nlohmann::json& response);

    // 预处理文本：截断超长文本。
    std::string preprocessText(const std::string& text) const;
};

} // namespace retrieval
