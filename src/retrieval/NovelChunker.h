#pragma once

// 小说文本智能切分器。
//
// 职责：将长篇小说内容切分为适合嵌入的文本块（chunk）。
// 切分策略：
//   - 章节正文：按段落边界切分（纯文本），无空行或超长段落时按句子边界兜底
//   - 每个 chunk 500-2000 字
//   - 相邻 chunk 保留 10-20% 重叠（维持语义连贯性）
//   - 角色/设定/世界规则：拼接核心信息为单条可嵌入文本
//
// 使用示例:
//   NovelChunker chunker;
//   auto chunks = chunker.chunkChapter(chapter, markdown_content);
//   auto char_text = chunker.chunkCharacter(character);
//   auto setting_text = chunker.chunkSetting(setting);

#include <string>
#include <vector>

struct Chapter;
struct Character;
struct Setting;
struct WorldRule;

namespace retrieval {

// 文本块结构。
struct TextChunk {
    std::string id;               // 唯一标识，如 "ch-001-0"
    std::string text;             // 切分后的文本
    nlohmann::json metadata;      // 元数据: {type, source_id, chapter_id, chunk_index, ...}

    // 便捷工厂：创建章节文本块。
    //
    // chapter_id  来源章节 ID（如 "ch-001"）。
    // chunk_index 块在章节内的序号（从 0 起），参与生成块 ID。
    // text        切分后的文本。
    // @return 填好 id/metadata 的文本块。
    static TextChunk chapterChunk(
        const std::string& chapter_id,
        int chunk_index,
        const std::string& text);

    // 便捷工厂：创建角色嵌入文本块。
    //
    // character_id 来源角色 ID。
    // text         角色核心信息拼接文本（见 chunkCharacter）。
    // @return 填好 id/metadata 的文本块。
    static TextChunk characterChunk(
        const std::string& character_id,
        const std::string& text);

    // 便捷工厂：创建设定嵌入文本块。
    //
    // setting_id 来源设定 ID。
    // text       设定核心信息拼接文本（见 chunkSetting）。
    // @return 填好 id/metadata 的文本块。
    static TextChunk settingChunk(
        const std::string& setting_id,
        const std::string& text);

    // 便捷工厂：创建世界规则嵌入文本块。
    //
    // rule_id 来源规则 ID。
    // text    规则核心信息拼接文本（见 chunkWorldRule）。
    // @return 填好 id/metadata 的文本块。
    static TextChunk worldRuleChunk(
        const std::string& rule_id,
        const std::string& text);
};

// 小说切分器 — 将小说内容切分为适合嵌入的文本块。
class NovelChunker {
public:
    NovelChunker() = default;

    // 配置切分参数。
    //
    // min_chunk_size 最小 chunk 大小（字符数），默认 500。
    // max_chunk_size 最大 chunk 大小（字符数），默认 2000。
    // overlap_ratio  相邻 chunk 重叠比例，默认 0.15 (15%)。
    void configure(int min_chunk_size = 500,
                   int max_chunk_size = 2000,
                   double overlap_ratio = 0.15);

    // ── 章节切分 ──

    // 将章节 Markdown 正文切分为文本块列表。
    //
    // 一律按纯文本处理：段落边界切分，超长段落按句子边界兜底，不依赖 markdown 结构。
    //
    // chapter          章节元数据（含 scenes[]）。
    // markdown_content 章节 Markdown 全文。
    // @return 文本块列表（按章节顺序排列）。
    std::vector<TextChunk> chunkChapter(
        const Chapter& chapter,
        const std::string& markdown_content) const;

    // ── 实体嵌入文本生成 ──

    // 拼接角色核心信息为单条可嵌入文本。
    //
    // 包含：name, role, goal, motivation, personality, traits,
    //       internal_conflict, external_conflict, speaking_style, arc
    //
    // character 源角色对象。
    // @return 拼接后的可嵌入文本。
    static std::string chunkCharacter(const Character& character);

    // 拼接设定核心信息为单条可嵌入文本。
    //
    // 包含：name, category, description, story_function, sensory_profile
    //
    // setting 源设定对象。
    // @return 拼接后的可嵌入文本。
    static std::string chunkSetting(const Setting& setting);

    // 拼接世界规则核心信息为单条可嵌入文本。
    //
    // 包含：name, summary, limitations, costs, exceptions
    //
    // rule 源世界规则对象。
    // @return 拼接后的可嵌入文本。
    static std::string chunkWorldRule(const WorldRule& rule);

private:
    int min_chunk_size_ = 500;
    int max_chunk_size_ = 2000;
    double overlap_ratio_ = 0.15;

    // 将长文本按段落边界切分为 chunk 列表。
    //
    // text      输入文本。
    // source_id 来源 ID（用于生成 chunk id）。
    // @return 文本块列表。
    std::vector<TextChunk> chunkByParagraphs(
        const std::string& text,
        const std::string& source_id) const;

    // 将文本切分为段落列表（按空行或单换行）。
    static std::vector<std::string> splitParagraphs(const std::string& text);

    // 按中英文句末标点将文本切分为句子列表（切点位于标点之后，标点保留）。
    std::vector<std::string> splitSentences(const std::string& text) const;

    // 生成 chunk 重叠文本（从上一块的末尾取 overlap_ratio 比例的内容）。
    std::string overlapFromPrevious(const std::string& prev_chunk_text) const;
};

} // namespace retrieval
