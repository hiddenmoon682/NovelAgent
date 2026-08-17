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
#include <mutex>
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
// 线程安全：generateEmbedding/generateEmbeddings/dimension 内部以 std::mutex
// 互斥，支持跨线程并发调用（网络密集，调用会串行等待，性能影响可接受）。
class EmbeddingGenerator : public IEmbeddingGenerator {
public:
    // 构造生成器，使用默认 EmbeddingConfig。
    //
    // 不发起网络请求；API Key 为空时仅告警，到首次 generateEmbeddings 才报错。
    //
    // @param provider_config LLM 提供商配置（复用 base_url + api_key），按值拷贝保存。
    explicit EmbeddingGenerator(const ProviderConfig& provider_config);

    // 带嵌入配置的构造函数。
    //
    // @param provider_config LLM 提供商配置，按值拷贝保存。
    // @param embed_config    嵌入请求配置（模型名/批量上限/截断长度），按值拷贝保存。
    EmbeddingGenerator(const ProviderConfig& provider_config,
                       const EmbeddingConfig& embed_config);

    ~EmbeddingGenerator() override = default;

    // ── IEmbeddingGenerator 接口实现 ──

    // 为单条文本生成嵌入向量（内部委托批量版本）。
    //
    // @param text 待嵌入文本，超过 max_text_length 时自动截断。
    // @return 嵌入向量。
    // @throws std::runtime_error API Key 未配置、网络/API 错误或响应为空时抛出。
    std::vector<float> generateEmbedding(const std::string& text) override;

    // 为多条文本批量生成嵌入向量（按 max_batch_size 自动分批请求）。
    //
    // @param texts 待嵌入文本列表，空列表直接返回空结果；超长文本自动截断。
    // @return 与输入顺序对应的嵌入向量列表。
    // @throws std::runtime_error API Key 未配置或任一批次请求/解析失败时抛出。
    std::vector<std::vector<float>> generateEmbeddings(
        const std::vector<std::string>& texts) override;

    // 返回嵌入向量的维度（首次成功调用后确定，此前返回 0）。
    int dimension() const override;

    // 返回嵌入模型名称。
    std::string modelName() const override { return embed_config_.model; }

    // 返回当前嵌入配置（只读引用，生命周期与本对象一致）。
    const EmbeddingConfig& embedConfig() const { return embed_config_; }

private:
    ProviderConfig provider_config_;
    EmbeddingConfig embed_config_;
    // 内部互斥：串行化并发访问（dimension_ 读写 + 共享 http_ 的请求），
    // 同一实例可被索引池线程与工具线程同时调用。
    mutable std::mutex mutex_;
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
