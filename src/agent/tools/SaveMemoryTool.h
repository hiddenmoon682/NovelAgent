#pragma once

// save_memory 工具 — 允许 LLM 主动将持久事实写入长期记忆日志。
//
// 写入策略（日志为事实源，向量为派生索引）：
//   1. 先追加到 LongTermMemoryStore（立即持久化，不可丢失）
//   2. 再尽力嵌入并写入向量库（失败不影响日志，下次索引自动补齐）
//
// 依赖：LongTermMemoryStore（必需）+ IVectorStore/IEmbeddingGenerator（可选）

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}

namespace agent {

class SaveMemoryTool : public BuiltInTool {
public:
    explicit SaveMemoryTool(const ToolDependencies& deps);

    std::string name() const override { return "save_memory"; }
    std::string description() const override {
        return "将重要信息写入跨会话的长期记忆。"
               "当出现以下情况时主动调用："
               "确立了新的关键设定或伏笔、用户表达了写作偏好、"
               "发生了后续章节必须一致的重要剧情事件。"
               "记忆会被永久保存并可通过 search_memory 检索。";
    }
    std::string brief() const override { return "写入长期记忆"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::System; }

private:
    LongTermMemoryStore* memory_store_;
    retrieval::IVectorStore* vector_store_;
    retrieval::IEmbeddingGenerator* embedding_gen_;
};

} // namespace agent
