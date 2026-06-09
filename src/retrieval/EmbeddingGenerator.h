#pragma once

/// 嵌入向量生成器 — 调用 LLM API 的 embeddings endpoint。
///
/// 复用 LLM 配置的 base_url + api_key，与 Chat API 共用认证凭据。
/// 支持单条和批量文本嵌入。
///
/// 支持的嵌入模型（OpenAI 兼容）:
///   - DeepSeek: text-embedding-3-small (默认)
///   - Kimi: 通过 /v1/embeddings endpoint
///
/// 使用示例:
///   EmbeddingGenerator gen(config);
///   auto vec = gen.generateEmbedding("主角进入古墓");
///   auto vecs = gen.generateEmbeddings({"文本1", "文本2", "文本3"});

#include "config/AppConfig.h"

#include <string>
#include <vector>

namespace retrieval {

/// 嵌入生成请求配置。
struct EmbeddingConfig {
    std::string model = "text-embedding-3-small";  // 嵌入模型名称
    int max_batch_size = 100;                       // 单次 API 调用最大文本数
    int max_text_length = 8000;                     // 单条文本最大字符数（超长截断）
};

/// 嵌入向量生成器。
///
/// 线程安全：不安全。同一实例不应并发调用。
class EmbeddingGenerator {
public:
    /// 构造函数，接收 ProviderConfig（复用 base_url + api_key）。
    /// @param provider_config LLM Provider 配置
    explicit EmbeddingGenerator(const ProviderConfig& provider_config);

    /// 带嵌入配置的构造函数。
    EmbeddingGenerator(const ProviderConfig& provider_config,
                       const EmbeddingConfig& embed_config);

    ~EmbeddingGenerator() = default;

    // ── 嵌入生成 ──

    /// 为单条文本生成嵌入向量。
    /// @param text 输入文本（超长自动截断）
    /// @return     浮点嵌入向量
    /// @throws std::runtime_error API 调用失败时
    std::vector<float> generateEmbedding(const std::string& text);

    /// 为多条文本批量生成嵌入向量。
    /// 若文本数超过 max_batch_size，自动拆分为多次 API 调用。
    /// @param texts 输入文本列表
    /// @return      嵌入向量列表（与输入顺序一致）
    /// @throws std::runtime_error API 调用失败时
    std::vector<std::vector<float>> generateEmbeddings(
        const std::vector<std::string>& texts);

    /// 返回嵌入向量的维度（首次调用后确定）。
    int dimension() const { return dimension_; }

    /// 返回当前嵌入配置。
    const EmbeddingConfig& embedConfig() const { return embed_config_; }

private:
    ProviderConfig provider_config_;
    EmbeddingConfig embed_config_;
    int dimension_ = 0;

    /// 发送 POST /v1/embeddings 请求。
    /// @param texts 文本列表
    /// @return      API 返回的 JSON 响应
    /// @throws std::runtime_error 网络错误、API 错误
    nlohmann::json sendEmbeddingRequest(
        const std::vector<std::string>& texts) const;

    /// 从 API 响应的 JSON 中提取嵌入向量列表。
    static std::vector<std::vector<float>> parseEmbeddingsResponse(
        const nlohmann::json& response);

    /// 预处理文本：截断超长文本。
    std::string preprocessText(const std::string& text) const;
};

} // namespace retrieval
