#pragma once

// ProjectIndexService — 基于内容哈希清单的增量索引服务。
//
// 时效性保证（详见 IndexManifest 注释）：
//   - 增量：源内容哈希未变则跳过重嵌入
//   - 孤儿清理：源删除后遗留向量随下次索引移除
//   - 模型指纹：嵌入模型/维度变化时整库失效重建
//
// 索引源（source_key 前缀）：
//   chapter:<id>  章节 Markdown 正文（NovelChunker 切分为多 chunk）
//   char:<id>     角色核心信息（单 chunk）
//   setting:<id>  设定核心信息（单 chunk）
//   rule:<id>     世界规则核心信息（单 chunk）
//   memory:<id>   长期记忆条目（单 chunk，可选注入）

#include "agent/index/IIndexService.h"
#include "agent/index/IndexManifest.h"

#include <memory>

struct Project;
namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}

namespace agent {

class LongTermMemoryStore;

class ProjectIndexService : public IIndexService {
public:
    // memory_store 可为 nullptr（不索引长期记忆）。
    ProjectIndexService(std::shared_ptr<Project> project,
                        retrieval::IVectorStore& vs,
                        retrieval::IEmbeddingGenerator& eg,
                        LongTermMemoryStore* memory_store = nullptr);

    IndexResult indexAll(
        std::function<void(const std::string&)> progress = nullptr,
        bool force = false) override;

private:
    std::string manifestPath() const;

    std::shared_ptr<Project> project_;
    retrieval::IVectorStore& vector_store_;
    retrieval::IEmbeddingGenerator& embedding_gen_;
    LongTermMemoryStore* memory_store_ = nullptr;
};

} // namespace agent
