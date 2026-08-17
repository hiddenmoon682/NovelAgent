// SearchMemoryTool 实现 — 显式语义搜索，允许 LLM 主动查询向量存储。

#include "agent/tools/SearchMemoryTools.h"

#include "retrieval/IVectorStore.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

namespace {
// 回传 LLM 的结果文本做 UTF-8 安全截断：
// 压缩摘要类长文本会占用大量上下文，超限部分按字节截断并回退到最近
// 字符边界（不拆坏多字节字符），末尾追加省略号提示截断。
std::string utf8Truncate(const std::string& s, size_t max_bytes)
{
    if (s.size() <= max_bytes) return s;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end) + "…";
}
} // namespace

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
        return {{"error", "向量检索后端未初始化（项目未打开）。"}};
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
            item["text"]       = utf8Truncate(r.metadata.value("text", ""), 500);
            item["type"]       = r.metadata.value("type", "");
            if (r.metadata.contains("chapter_id")) {
                item["chapter_id"] = r.metadata["chapter_id"];
            }
            // 长期记忆条目附带类型与创建时间，供 LLM 判断新旧
            if (r.metadata.contains("kind")) {
                item["kind"] = r.metadata["kind"];
            }
            if (r.metadata.contains("created_at")) {
                item["created_at"] = r.metadata["created_at"];
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
