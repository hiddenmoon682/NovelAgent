#pragma once

// 语义搜索工具 — 允许 LLM 主动查询向量存储中的章节片段。
//
// 与 ContextManager::assemble 中的自动检索不同，此工具让 LLM 可以：
// - 用自定义查询词搜索（不同于用户最后一条消息）
// - 多次调用，每次使用不同的查询角度
// - 自行决定是否需要搜索，避免不必要的上下文注入
//
// 依赖：IVectorStore + IEmbeddingGenerator（由 NovelAgentApp 注入）
// 注册方式：REGISTER_TOOL_NP 宏（不需要 Project 指针）

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

// 前向声明
namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}

namespace agent {

// 语义搜索工具。
// 参数: query (string) — 自然语言搜索查询
//       top_k (integer, 可选) — 返回结果数量，默认 5，范围 [1, 20]
// 返回: { results: [{ id, similarity, text, chapter_id }], query }
class SearchMemoryTool : public BuiltInTool {
public:
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
};

// 初始化 search_memory 工具的后端指针。
//
// 必须在 BuiltInTool::registerAllTo() 之前调用，
// 否则工具实例化时后端指针为空，execute() 将返回错误。
//
// vs  向量存储实例（非拥有指针，生命周期由调用方管理）
// eg  嵌入生成器实例（非拥有指针，生命周期由调用方管理）
void initSearchMemoryBackend(retrieval::IVectorStore* vs,
                              retrieval::IEmbeddingGenerator* eg);

} // namespace agent
