// SearchMemoryTool 实现 — 显式语义搜索，允许 LLM 主动查询向量存储。

#include "agent/tools/SearchMemoryTools.h"

#include "retrieval/IVectorStore.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

SearchMemoryTool::SearchMemoryTool(const ToolDependencies& deps)
    : vector_store_(deps.vector_store)
    , embedding_gen_(deps.embedding_gen)
{}

json SearchMemoryTool::parameters() const {
    return utils::schema::object({
        {"query", utils::schema::stringProp(
            "自然语言搜索查询，描述你要查找的情节、对话或设定内容")},
        {"top_k", utils::schema::integerProp(
            "返回结果数量（1-20，默认 5）")}
    }, {"query"});
}

json SearchMemoryTool::execute(const json& args) {
    if (!vector_store_ || !embedding_gen_) {
        return {{"error", "向量检索后端未初始化。请先执行 /index 命令构建索引。"}};
    }

    std::string query = args.value("query", "");
    if (query.empty()) {
        return {{"error", "查询内容不能为空"}};
    }

    int top_k = args.value("top_k", 5);
    if (top_k < 1)  top_k = 1;
    if (top_k > 20) top_k = 20;

    spdlog::info("[search_memory] query=\"{}\" top_k={}", query, top_k);

    try {
        auto query_emb = embedding_gen_->generateEmbedding(query);
        auto results = vector_store_->search(query_emb, top_k);

        json arr = json::array();
        for (const auto& r : results) {
            json item;
            item["id"]         = r.id;
            item["similarity"] = r.similarity;
            item["text"]       = r.metadata.value("text", "");
            if (r.metadata.contains("chapter_id")) {
                item["chapter_id"] = r.metadata["chapter_id"];
            }
            arr.push_back(std::move(item));
        }

        spdlog::info("[search_memory] 返回 {} 条结果", arr.size());
        return {
            {"results", std::move(arr)},
            {"query", query}
        };

    } catch (const std::exception& e) {
        spdlog::error("[search_memory] 检索异常: {}", e.what());
        return {{"error", std::string("检索失败: ") + e.what()}};
    }
}

} // namespace agent

REGISTER_TOOL_DEPS(agent::SearchMemoryTool, "search_memory", search_memory)
