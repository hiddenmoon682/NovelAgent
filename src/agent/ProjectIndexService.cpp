#include "agent/ProjectIndexService.h"

#include "project/Models/Project.h"
#include "project/ProjectIO.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "retrieval/IVectorStore.h"
#include "retrieval/NovelChunker.h"

namespace agent {

ProjectIndexService::ProjectIndexService(
    std::shared_ptr<Project> project,
    retrieval::IVectorStore& vs,
    retrieval::IEmbeddingGenerator& eg)
    : project_(std::move(project))
    , vector_store_(vs)
    , embedding_gen_(eg)
{}

IndexResult ProjectIndexService::indexAll(
    std::function<void(const std::string&)> progress)
{
    IndexResult result;

    if (!project_ || project_->path.empty()) {
        result.error = "未打开项目";
        return result;
    }

    auto report = [&](const std::string& msg) {
        if (progress) progress(msg);
    };

    report("正在为项目内容建立向量索引...");

    retrieval::NovelChunker chunker;
    std::vector<retrieval::TextChunk> all_chunks;

    for (const auto& ch : project_->outline.chapters) {
        if (ch.file_path.empty()) continue;
        std::string md = ProjectIO::readChapter(project_->path, ch.file_path);
        if (md.empty()) continue;
        auto chunks = chunker.chunkChapter(ch, md);
        for (auto& c : chunks) all_chunks.push_back(std::move(c));
        ++result.chapters;
    }
    report("  章节: " + std::to_string(result.chapters) + " 章 → "
         + std::to_string(all_chunks.size()) + " 个片段");

    for (const auto& c : project_->characters) {
        std::string text = retrieval::NovelChunker::chunkCharacter(c);
        if (text.empty()) continue;
        all_chunks.push_back(retrieval::TextChunk::characterChunk(c.id, text));
        ++result.characters;
    }
    report("  角色: " + std::to_string(result.characters) + " 个");

    for (const auto& s : project_->settings) {
        std::string text = retrieval::NovelChunker::chunkSetting(s);
        if (text.empty()) continue;
        all_chunks.push_back(retrieval::TextChunk::settingChunk(s.id, text));
        ++result.settings;
    }
    report("  设定: " + std::to_string(result.settings) + " 个");

    for (const auto& r : project_->world_rules) {
        std::string text = retrieval::NovelChunker::chunkWorldRule(r);
        if (text.empty()) continue;
        all_chunks.push_back(retrieval::TextChunk::worldRuleChunk(r.id, text));
        ++result.world_rules;
    }
    report("  世界规则: " + std::to_string(result.world_rules) + " 条");

    result.total_chunks = static_cast<int>(all_chunks.size());
    if (all_chunks.empty()) {
        result.error = "没有可索引的内容";
        return result;
    }

    report("正在生成嵌入向量 (" + std::to_string(all_chunks.size()) + " 条)...");
    std::vector<std::string> texts;
    texts.reserve(all_chunks.size());
    for (const auto& c : all_chunks) texts.push_back(c.text);
    auto embeddings = embedding_gen_.generateEmbeddings(texts);

    if (embeddings.size() != all_chunks.size()) {
        result.error = "嵌入向量数量不匹配: " + std::to_string(embeddings.size())
                     + " vs " + std::to_string(all_chunks.size());
        return result;
    }

    for (size_t i = 0; i < all_chunks.size(); ++i) {
        vector_store_.insert(all_chunks[i].id, embeddings[i], all_chunks[i].metadata);
    }
    vector_store_.flush();

    report("向量索引已建立: " + std::to_string(all_chunks.size()) + " 条 → "
         + project_->path + "/.novelagent/vectors.json");
    return result;
}

} // namespace agent
