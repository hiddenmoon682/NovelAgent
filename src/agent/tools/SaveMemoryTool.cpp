// SaveMemoryTool 实现 — 长期记忆写入（日志优先，向量尽力）。

#include "agent/tools/SaveMemoryTool.h"

#include "agent/memory/LongTermMemoryStore.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "retrieval/IVectorStore.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace agent {
using json = nlohmann::json;

SaveMemoryTool::SaveMemoryTool(const ToolDependencies& deps)
    : memory_store_(deps.memory_store)
    , vector_store_(deps.vector_store)
    , embedding_gen_(deps.embedding_gen)
{}

json SaveMemoryTool::parameters() const {
    return utils::schema::object({
        {"content", utils::schema::stringProp(
            "要记住的内容。用一到三句话完整描述事实，"
            "包含必要的上下文（涉及的角色/章节/设定名称）")},
        {"kind", utils::schema::stringProp(
            "记忆类型: fact(设定事实) / preference(用户偏好) / event(剧情事件)，默认 fact")}
    }, {"content"});
}

json SaveMemoryTool::execute(const json& args) {
    if (!memory_store_ || !memory_store_->initialized()) {
        return {{"error", "长期记忆存储未初始化（项目未打开）"}};
    }

    std::string content = args.value("content", "");
    if (content.empty()) {
        return {{"error", "记忆内容不能为空"}};
    }
    std::string kind = args.value("kind", "fact");
    if (kind != "fact" && kind != "preference" && kind != "event") {
        kind = "fact";
    }

    // 1. 日志持久化（事实源，必须成功）
    std::string id = memory_store_->append(content, kind);
    if (id.empty()) {
        return {{"error", "写入长期记忆失败"}};
    }

    // 2. 向量索引（派生数据，尽力而为——失败时下次增量索引自动补齐）
    bool indexed = false;
    if (vector_store_ && embedding_gen_) {
        try {
            auto emb = embedding_gen_->generateEmbedding(content);
            json metadata = {
                {"type", "memory"},
                {"memory_id", id},
                {"kind", kind},
                {"text", content}
            };
            vector_store_->insert("memory-" + id, emb, metadata);
            vector_store_->flush();
            indexed = true;
        } catch (const std::exception& e) {
            spdlog::warn("[save_memory] 即时嵌入失败（下次索引补齐）: {}", e.what());
        }
    }

    spdlog::info("[save_memory] 已保存记忆 {} (kind={}, indexed={})", id, kind, indexed);
    return {
        {"memory_id", id},
        {"kind", kind},
        {"indexed", indexed}
    };
}

} // namespace agent

REGISTER_TOOL_DEPS(agent::SaveMemoryTool, "save_memory", save_memory)
