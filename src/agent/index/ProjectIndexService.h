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
    // 构造索引服务。
    // @param project 项目数据（共享所有权），索引时读取章节/角色/设定/规则。
    // @param vs 向量库（vector store）；非拥有引用，调用方保证其存活期
    //           覆盖本服务生命周期。
    // @param eg 嵌入生成器（embedding generator）；非拥有引用，存活期约定同上。
    // @param memory_store 长期记忆日志；非拥有指针，可为 nullptr（不索引长期记忆），
    //                     非空时调用方保证其存活期覆盖本服务。
    ProjectIndexService(std::shared_ptr<Project> project,
                        retrieval::IVectorStore& vs,
                        retrieval::IEmbeddingGenerator& eg,
                        LongTermMemoryStore* memory_store = nullptr);

    // 增量建立/更新全项目向量索引（完整语义见 IIndexService::indexAll）。
    // @param progress 可选进度回调，每完成一个阶段以中文描述调用一次。
    // @param force false（默认）= 按内容哈希增量，未变更的源跳过重嵌入；
    //              true = 忽略清单，强制整库全量重建。
    // @return 索引统计；error 非空表示失败。
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
