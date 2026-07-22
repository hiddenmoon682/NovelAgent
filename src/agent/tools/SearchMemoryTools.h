#pragma once

// 语义搜索工具 — 允许 LLM 主动查询向量存储中的章节片段。
//
// 依赖：IVectorStore + IEmbeddingGenerator（通过 ToolDependencies 构造注入）
// 注册方式：REGISTER_TOOL_DEPS 宏

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}

namespace agent {

class SearchMemoryTool : public BuiltInTool {
public:
    explicit SearchMemoryTool(const ToolDependencies& deps);

    std::string name() const override { return "search_memory"; }
    std::string description() const override {
        return "对已索引的章节内容执行语义搜索。"
               "用自然语言描述你想查找的情节、对话或设定，"
               "返回最匹配的章节片段及其相似度分数。"
               "当你需要确认过去的设定、情节细节或对话内容时主动调用。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::System; }

private:
    retrieval::IVectorStore* vector_store_;
    retrieval::IEmbeddingGenerator* embedding_gen_;
};

} // namespace agent
