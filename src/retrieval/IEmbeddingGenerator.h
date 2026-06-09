#pragma once

/// 嵌入向量生成器抽象接口 — 解耦嵌入生成与具体 API 实现。
///
/// 当前实现：ApiEmbeddingGenerator（OpenAI 兼容 /v1/embeddings）
/// 未来可选：LocalEmbeddingGenerator（本地 ONNX 模型，离线免费用）
///
/// 所有需要嵌入向量的模块均通过此接口交互。

#include <string>
#include <vector>

namespace retrieval {

/// 嵌入生成器抽象接口。
class IEmbeddingGenerator {
public:
    virtual ~IEmbeddingGenerator() = default;

    /// 为单条文本生成嵌入向量。
    virtual std::vector<float> generateEmbedding(const std::string& text) = 0;

    /// 为多条文本批量生成嵌入向量。
    virtual std::vector<std::vector<float>> generateEmbeddings(
        const std::vector<std::string>& texts) = 0;

    /// 返回嵌入向量的维度（首次调用后确定）。
    virtual int dimension() const = 0;
};

} // namespace retrieval
