/// SearchMemoryTool 实现 — 显式语义搜索，允许 LLM 主动查询向量存储。

#include "agent/tools/SearchMemoryTools.h"

#include "retrieval/IVectorStore.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>

namespace agent {
using json = nlohmann::json;

// ===========================================================================
// 静态后端指针 — 由 initSearchMemoryBackend() 设置
//
// 非拥有指针，生命周期由 NovelAgentApp 管理。
// NovelAgentApp::vector_store_ / embedding_gen_ 是值成员，
// 在 registry_（持有工具实例）之后析构，不会出现悬垂指针。
// ===========================================================================
namespace {
    std::atomic<retrieval::IVectorStore*>        g_vector_store{nullptr};
    std::atomic<retrieval::IEmbeddingGenerator*> g_embedding_gen{nullptr};
} // anonymous namespace

void initSearchMemoryBackend(retrieval::IVectorStore* vs,
                              retrieval::IEmbeddingGenerator* eg) {
    g_vector_store.store(vs, std::memory_order_release);
    g_embedding_gen.store(eg, std::memory_order_release);
}

// ===========================================================================
// parameters — 定义 search_memory 的 JSON Schema
// ===========================================================================

json SearchMemoryTool::parameters() const {
    return utils::schema::object({
        {"query", utils::schema::stringProp(
            "自然语言搜索查询，描述你要查找的情节、对话或设定内容")},
        {"top_k", utils::schema::integerProp(
            "返回结果数量（1-20，默认 5）")}
    }, {"query"});
}

// ===========================================================================
// execute — 执行语义搜索
//
// 流程：
//   1. 校验后端指针（未初始化 → 返回错误）
//   2. 校验 query 非空
//   3. clamp top_k 到 [1, 20]
//   4. embedding_gen_ → generateEmbedding(query) 生成查询向量
//   5. vector_store_  → search(query_vec, top_k) 执行语义搜索
//   6. 格式化结果 → { results: [...], query }
//
// 异常安全：generateEmbedding / search 失败时捕获异常，返回错误信息，
//           不阻断 Agent 主流程。
// ===========================================================================

json SearchMemoryTool::execute(const json& args) {
    // ── 前置检查：后端指针 ──────────────────────────────────────────────
    auto* vs = g_vector_store.load(std::memory_order_acquire);
    auto* eg = g_embedding_gen.load(std::memory_order_acquire);
    if (!vs || !eg) {
        return {{"error", "向量检索后端未初始化。请先执行 /index 命令构建索引。"}};
    }

    // ── 参数校验 ────────────────────────────────────────────────────────
    std::string query = args.value("query", "");
    if (query.empty()) {
        return {{"error", "查询内容不能为空"}};
    }

    int top_k = args.value("top_k", 5);
    if (top_k < 1)  top_k = 1;
    if (top_k > 20) top_k = 20;

    spdlog::info("[search_memory] query=\"{}\" top_k={}", query, top_k);

    // ── 生成查询向量 + 语义搜索 ──────────────────────────────────────────
    try {
        auto query_emb = eg->generateEmbedding(query);
        auto results = vs->search(query_emb, top_k);

        // 格式化结果：提取 id / similarity / text / chapter_id
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

// ===========================================================================
// 手动工厂注册 — 不接收 Project&，工厂 lambda 忽略 shared_ptr<Project> 参数
// （仿 ShellTools.cpp 的 ShellTools 注册模式）
// ===========================================================================
namespace {
    static const bool _reg_SearchMemory = []() {
        agent::BuiltInTool::registerFactory("search_memory",
            [](std::shared_ptr<Project>) -> std::unique_ptr<agent::BuiltInTool> {
                return std::make_unique<agent::SearchMemoryTool>();
            });
        return true;
    }();
}
