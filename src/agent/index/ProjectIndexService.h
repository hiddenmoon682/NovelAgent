#pragma once

// ProjectIndexService — 基于内容哈希清单的增量索引服务（SQLite 实现）。
//
// 时效性保证（清单存于 novel.db 的 index_sources/index_chunks/kv_store）：
//   - 增量：源内容哈希未变则跳过重嵌入
//   - 孤儿清理：源删除后遗留向量随下次索引移除
//   - 模型指纹：嵌入模型/维度变化时整库失效重建（DROP vec_chunks + 清空清单）
//
// 索引源（source_key 前缀）：
//   chapter:<id>  章节正文纯文本（NovelChunker 切分为多 chunk）
//   char:<id>     角色核心信息（单 chunk）
//   setting:<id>  设定核心信息（单 chunk）
//   rule:<id>     世界规则核心信息（单 chunk）
//   memory:<id>   长期记忆条目（单 chunk，可选注入）
//
// 事务模型：indexAll 的整库失效与批量写入均为单事务（经 SqliteStore）。
// 向量写入直连 SQL，不再经 IVectorStore（避免与检索侧共享锁嵌套）。

#include "agent/index/IIndexService.h"

#include <memory>
#include <mutex>

class ProjectAccess;
namespace storage { class SqliteStore; }
namespace retrieval { class IEmbeddingGenerator; }

namespace agent {

class LongTermMemoryStore;

class ProjectIndexService : public IIndexService {
public:
    // @param access 项目受控访问层（P2/P3：索引只读经 withReadLock 快照）。
    // @param sqlite SQLite 单库（清单表与向量表所在库）；非拥有引用，
    //               调用方保证其存活期覆盖本服务生命周期。
    // @param eg 嵌入生成器；非拥有引用，存活期约定同上。
    // @param memory_store 长期记忆日志；非拥有指针，可为 nullptr。
    ProjectIndexService(std::shared_ptr<ProjectAccess> access,
                        storage::SqliteStore& sqlite,
                        retrieval::IEmbeddingGenerator& eg,
                        LongTermMemoryStore* memory_store = nullptr);

    IndexResult indexAll(
        std::function<void(const std::string&)> progress = nullptr,
        bool force = false) override;

private:
    std::shared_ptr<ProjectAccess> project_access_;
    storage::SqliteStore& sqlite_;
    retrieval::IEmbeddingGenerator& embedding_gen_;
    LongTermMemoryStore* memory_store_ = nullptr;
    std::mutex index_mutex_;  // E8：indexAll 内部串行化（多会话完成回调并发调用）
};

} // namespace agent