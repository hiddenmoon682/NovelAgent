#pragma once

// 小说文本智能切分器。
//
// 职责：将长篇小说内容切分为适合嵌入的文本块（chunk）。
// 切分策略：
//   - 章节正文：按换行切分段落（纯文本）：单换行一行一段，空行合并；超长段落按句子边界兜底
//   - 每个 chunk 约 600-1800 字节（≈200-600 字，按 期望字数×3 换算；中文 1 字 3 字节）
//   - 相邻 chunk 保留重叠（默认 15%，可配置 0-30%）
//   - 角色/设定/世界规则：拼接核心信息为单条可嵌入文本
//   - 内部统一以 UTF-32（码点）处理中文字符，输入输出均为 UTF-8
//
// 使用示例:
//   NovelChunker chunker;
//   auto chunks = chunker.chunkChapter(chapter, content);
//   auto char_text = chunker.chunkCharacter(character);
//   auto setting_text = chunker.chunkSetting(setting);

#include <map>
#include <string>
#include <vector>

struct Chapter;
struct Character;
struct Setting;
struct WorldRule;

namespace retrieval {

// 章节上下文（id → 名字 字典），供块头注入使用。
// Chapter::pov_characters / focus_characters / location_id 存的是实体 id，
// 索引时由 ProjectIndexService 从项目数据解析为名字填入后传入。
struct ChapterContext {
    std::map<std::string, std::string> character_names;  // 角色 id → 名字
    std::map<std::string, std::string> setting_names;    // 设定 id → 名字
};

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
    // min_chunk_size 最小 chunk 大小（字节数，按 UTF-8 字节计量，中文 1 字 3 字节）。
    //                 默认 600（≈200 字）；可用"期望字数 × 3"换算调整。
    // max_chunk_size 最大 chunk 大小（字节数）。默认 1800（≈600 字）。
    // overlap_ratio  相邻 chunk 重叠比例，默认 0.15 (15%)。
    void configure(int min_chunk_size = 600,
                   int max_chunk_size = 1800,
                   double overlap_ratio = 0.15);

    // ── 章节切分 ──

    // 将章节正文（纯文本）切分为文本块列表。
    //
    // 一律按纯文本处理：按换行切分段落（单换行一行一段、空行合并），
    // 超长段落按句子边界兜底，不依赖 markdown 结构
    // （真实小说以 .txt/.docx 纯文本形式存储，正文不含 markdown 标记）。
    //
    // chapter  源章节（取 id 生成块 ID）。
    // content  章节正文全文（纯文本）。
    // ctx      章节上下文（实体 id → 名字），非空时向每个块注入精简头。
    // @return 文本块列表（按章节顺序排列）。
    std::vector<TextChunk> chunkChapter(
        const Chapter& chapter,
        const std::string& content,
        const ChapterContext& ctx = {}) const;

    // ── 实体嵌入文本生成 ──

    // 拼接角色核心信息为单条可嵌入文本。
    //
    // 委托 Character::toEmbeddingText()；字段清单与文案由模型自身维护，
    // 调整角色字段时无需修改本类。
    //
    // character 源角色对象。
    // @return 拼接后的可嵌入文本。
    static std::string chunkCharacter(const Character& character);

    // 拼接设定核心信息为单条可嵌入文本。
    //
    // 委托 Setting::toEmbeddingText()；字段清单与文案由模型自身维护，
    // 调整设定字段时无需修改本类。
    //
    // setting 源设定对象。
    // @return 拼接后的可嵌入文本。
    static std::string chunkSetting(const Setting& setting);

    // 拼接世界规则核心信息为单条可嵌入文本。
    //
    // 委托 WorldRule::toEmbeddingText()；字段清单与文案由模型自身维护，
    // 调整世界规则字段时无需修改本类。
    //
    // rule 源世界规则对象。
    // @return 拼接后的可嵌入文本。
    static std::string chunkWorldRule(const WorldRule& rule);

private:
    int min_chunk_size_ = 600;   // 默认下限 ≈200 字（中文 1 字 3 字节）
    int max_chunk_size_ = 1800;  // 默认上限 ≈600 字
    double overlap_ratio_ = 0.15;

    // 将长文本按段落边界切分为 chunk 列表。
    //
    // 内部统一以 UTF-32（码点）处理：切段/找标点/硬切都按"字"进行，
    // 切点天然落在字符边界；块尺寸按 UTF-8 字节口径记账（与配置一致），
    // 输出时转回 UTF-8 字节串。
    //
    // text      输入文本（UTF-8）。
    // source_id 来源 ID（用于生成 chunk id）。
    // @return 文本块列表。
    std::vector<TextChunk> chunkByParagraphs(
        const std::string& text,
        const std::string& source_id) const;

    // 将文本切分为段落列表（按换行：单个换行即段落边界（一行一段），
    // 连续换行/空白行合并为一个分隔符）。
    static std::vector<std::u32string> splitParagraphs(const std::u32string& text);

    // 按中英文句末标点将文本切分为句子列表（切点位于标点之后，标点保留）。
    std::vector<std::u32string> splitSentences(const std::u32string& text) const;

    // 生成 chunk 重叠文本（按字节比例取上一块末尾，优先在句子边界截断；
    // 码点级操作，截断天然落在字符边界）。
    std::u32string overlapFromPrevious(const std::u32string& prev_chunk_text) const;
};

} // namespace retrieval
