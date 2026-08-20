// NovelChunker 实现 — 小说文本智能切分。

#include "retrieval/NovelChunker.h"

#include "project/Models.h"
#include "utils/Utf8Utils.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace {

// UTF-8 ⇄ UTF-32 转换与字节折算放 utils/Utf8Utils.h，此处直接暴露 unqualified 名
using namespace utils::utf8;

// 句末标点（码点级比较）：中文 。！？…；英文 . ! ?
bool isEndPunct(char32_t c)
{
    switch (c) {
        case U'。': case U'！': case U'？': case U'…':
        case U'.': case U'!': case U'?':
            return true;
        default:
            return false;
    }
}

// 返回 text 中最后一个句末标点之后的码点偏移（切点位于标点之后）；无标点返回 npos。
// 换行（段落边界）也视为合法切点，与旧实现 find_last_of(".。！？!?\n") 行为对齐。
size_t lastSentenceEnd(const std::u32string& text)
{
    size_t end = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == U'\n' || isEndPunct(text[i])) {
            end = i + 1;
        }
    }
    return end;
}

// 取 text 末尾"字节数最接近且不超过 byte_budget"的码点后缀（字符边界天然对齐）。
std::u32string tailByBytes(const std::u32string& text, size_t byte_budget)
{
    size_t bytes = 0;
    size_t start = text.size();
    while (start > 0) {
        const int cb = utf8ByteLength(text[start - 1]);
        if (bytes + cb > byte_budget) break;
        bytes += cb;
        --start;
    }
    return text.substr(start);
}

// 构造章节精简头（≤80 字节左右）：仅拼有值的行，全空返回空串。
// 行序固定：定位行 → 人物行 → 地点行 → 时间行。
// 注：Chapter 为全局前向声明，ChapterContext 在 retrieval 命名空间内，
// 匿名域（文件作用域）中需显式限定，无法 unqualified 引用。
std::string buildChapterHeader(const Chapter& ch, const retrieval::ChapterContext& ctx)
{
    std::ostringstream ss;

    // 定位行："第{order}章 {title}"；title 为空仍有 order 时保留"第{order}章"
    std::string first;
    if (ch.order > 0) first = "第" + std::to_string(ch.order) + "章";
    if (!ch.title.empty()) {
        if (!first.empty()) first += " ";
        first += ch.title;
    }
    if (!first.empty()) ss << first << "\n";

    // 人物行：pov 在前、focus 在后，按 id 解析名字，未知 id 跳过，去重后至多 6 个
    std::vector<std::string> names;
    std::set<std::string> seen;
    auto appendNames = [&](const std::vector<std::string>& ids) {
        for (const auto& id : ids) {
            const auto it = ctx.character_names.find(id);
            if (it == ctx.character_names.end()) continue;
            if (!seen.insert(it->second).second) continue;
            names.push_back(it->second);
        }
    };
    appendNames(ch.pov_characters);
    appendNames(ch.focus_characters);
    if (names.size() > 6) names.resize(6);
    if (!names.empty()) {
        ss << "人物：";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) ss << "、";
            ss << names[i];
        }
        ss << "\n";
    }

    // 单值行（地点/时间）：非空才拼，超过 20 字（码点）截断加省略号；
    // 码点级截断保证不切破多字节字符（与全文 UTF-8 处理口径一致）
    auto appendValueLine = [&](const std::string& label, const std::string& v) {
        if (v.empty()) return;
        const std::u32string u32 = utf8ToU32(v);
        if (u32.size() > 20) {
            ss << label << u32ToUtf8(u32.substr(0, 20)) << "…\n";
        } else {
            ss << label << v << "\n";
        }
    };
    const auto lit = ctx.setting_names.find(ch.location_id);
    if (lit != ctx.setting_names.end()) appendValueLine("地点：", lit->second);
    appendValueLine("时间：", ch.time_marker);

    std::string header = ss.str();
    if (!header.empty() && header.back() == '\n') header.pop_back();  // 与正文以单个 \n 连接
    return header;
}

} // namespace

namespace retrieval {

// ===========================================================================
// TextChunk 工厂方法
// ===========================================================================

TextChunk TextChunk::chapterChunk(
    const std::string& chapter_id,
    int chunk_index,
    const std::string& text)
{
    TextChunk chunk;
    chunk.id = chapter_id + "-" + std::to_string(chunk_index);
    chunk.text = text;
    chunk.metadata = {
        {"type", "chapter"},
        {"chapter_id", chapter_id},
        {"chunk_index", chunk_index},
        {"text", text}
    };
    return chunk;
}

TextChunk TextChunk::characterChunk(
    const std::string& character_id,
    const std::string& text)
{
    TextChunk chunk;
    chunk.id = "char-" + character_id;
    chunk.text = text;
    chunk.metadata = {
        {"type", "character"},
        {"character_id", character_id},
        {"text", text}
    };
    return chunk;
}

TextChunk TextChunk::settingChunk(
    const std::string& setting_id,
    const std::string& text)
{
    TextChunk chunk;
    chunk.id = "setting-" + setting_id;
    chunk.text = text;
    chunk.metadata = {
        {"type", "setting"},
        {"setting_id", setting_id},
        {"text", text}
    };
    return chunk;
}

TextChunk TextChunk::worldRuleChunk(
    const std::string& rule_id,
    const std::string& text)
{
    TextChunk chunk;
    chunk.id = "rule-" + rule_id;
    chunk.text = text;
    chunk.metadata = {
        {"type", "world_rule"},
        {"rule_id", rule_id},
        {"text", text}
    };
    return chunk;
}

// ===========================================================================
// 配置
// ===========================================================================

void NovelChunker::configure(int min_chunk_size, int max_chunk_size, double overlap_ratio)
{
    max_chunk_size_ = std::max(1, max_chunk_size);
    // 钳制上限：min > max 时封块条件 current_size >= min 永不满足，块会无限增长（巨型块复现）
    min_chunk_size_ = std::clamp(min_chunk_size, 0, max_chunk_size_);
    overlap_ratio_ = std::clamp(overlap_ratio, 0.0, 0.3);
}

// ===========================================================================
// 章节切分
// ===========================================================================

std::vector<TextChunk> NovelChunker::chunkChapter(
    const Chapter& chapter,
    const std::string& content,
    const ChapterContext& ctx) const
{
    if (content.empty()) {
        spdlog::debug("[NovelChunker] 章节 {} 内容为空，跳过切分", chapter.id);
        return {};
    }

    // 统一按纯文本段落切分：不依赖 markdown 场景标记（真实小说正文无此结构）。
    spdlog::debug("[NovelChunker] 按段落边界切分章节 {} (纯文本)", chapter.id);
    std::vector<TextChunk> chunks = chunkByParagraphs(content, chapter.id);

    // 精简头注入：切分/重叠均基于纯正文（header 不计入块大小预算），
    // 切分完成后对每个块 prepend，嵌入文本与展示文本（metadata["text"]）保持同一份。
    const std::string header = buildChapterHeader(chapter, ctx);
    if (!header.empty()) {
        for (auto& c : chunks) {
            c.text = header + "\n" + c.text;
            c.metadata["text"] = c.text;
        }
    }
    return chunks;
}

// ===========================================================================
// 实体嵌入文本生成
// ===========================================================================

std::string NovelChunker::chunkCharacter(const Character& character)
{
    return character.toEmbeddingText();
}

std::string NovelChunker::chunkSetting(const Setting& setting)
{
    return setting.toEmbeddingText();
}

std::string NovelChunker::chunkWorldRule(const WorldRule& rule)
{
    return rule.toEmbeddingText();
}

// ===========================================================================
// 切分策略实现
// ===========================================================================

// 将整章纯文本按段落边界切分为检索用块（核心切分入口）。
//
// 流程：段落切分 → 按字节预算聚合封块 → 超长单元码点硬切 → 句子级二次切分
// → 块间重叠。三层防御（段落/句子/码点）保证任何输入都不产出超出
// max_chunk_size_ 的巨型块；块尺寸按 UTF-8 字节记账（与 configure 口径一致），
// 所有截断点按码点对齐，不会切破多字节字符。
std::vector<TextChunk> NovelChunker::chunkByParagraphs(
    const std::string& text,
    const std::string& source_id) const
{
    // 统一转成 UTF-32（码点）后处理：下标/切分按"字"进行，切点天然落在字符边界，
    // 不再需要 UTF-8 续字节回退；块尺寸仍按 UTF-8 字节记账（与配置口径一致）。
    const std::u32string u32text = utf8ToU32(text);
    // 段落是聚合的最小单元：切块时只整段搬移，不会从段中腰斩（除非整段超长）
    auto paragraphs = splitParagraphs(u32text);

    std::vector<std::u32string> chunk_texts; // 已封块的块列表（后续统一转成 TextChunk）
    std::u32string current_chunk;            // 正在聚合中的当前块
    int current_size = 0;                    // 当前块的 UTF-8 字节数（与配置口径一致）

    // 追加到当前块：超过上限且满足下限时封块。
    // 双条件缺一不可：仅超上限（未达下限）时继续聚合，避免产出过碎的小块；
    // configure 已将 min 钳制到 [0, max]，保证"达下限"永不落空，不会无限增长。
    std::function<void(const std::u32string&, int)> appendToCurrent =
        [&](const std::u32string& seg, int seg_bytes) {
            if (current_size + seg_bytes > max_chunk_size_ && current_size >= min_chunk_size_) {
                chunk_texts.push_back(current_chunk);
                current_chunk.clear();
                current_size = 0;
            }
            if (!current_chunk.empty()) {
                current_chunk += U"\n\n";  // 块内段落间以空行分隔，保留正文的段落间隔感
                current_size += 2;         // 两个换行符的字节也计入预算
            }
            current_chunk += seg;
            current_size += seg_bytes;
        };

    // 追加一个可嵌单元（段落或句子）。
    // 超长单元（>max，多为无句末标点的长连续串，splitSentences 找不到切点）：
    // 按码点硬切成 ≤max 字节的片段再逐个聚合，避免产出超 max 的巨型块。
    std::function<void(const std::u32string&, int)> appendSegment =
        [&](const std::u32string& seg, int seg_bytes) {
            if (seg_bytes <= max_chunk_size_) {
                appendToCurrent(seg, seg_bytes);
                return;
            }
            size_t start = 0;
            while (start < seg.size()) {
                // 从 start 起取字节数不超过 max 的最长码点前缀
                size_t end = start;
                int bytes = 0;
                while (end < seg.size()) {
                    const int cb = utf8ByteLength(seg[end]);
                    if (bytes + cb > max_chunk_size_) break;
                    bytes += cb;
                    ++end;
                }
                if (end == start) {
                    // 极端：单个字符的字节数超过 max（如 max 极小时遇到中文）→
                    // 整体追加该字符，预算与字符粒度冲突时优先保证字符完整、避免死循环
                    appendToCurrent(seg.substr(start, 1), utf8ByteLength(seg[start]));
                    ++start;
                    continue;
                }
                appendSegment(seg.substr(start, end - start), bytes); // 前缀必 ≤ max，走 appendSegment 入口统一聚合并兜底
                start = end;
            }
        };

    for (const auto& para : paragraphs) {
        // 超长段落（如整章无空行时的"整章一段"）：按句子边界二次切分再聚合，
        // 避免单个 chunk 突破 max_chunk_size_，也避免从句子中间截断。
        if (utf8ByteCount(para) > static_cast<size_t>(max_chunk_size_)) {
            for (const auto& sentence : splitSentences(para)) {
                appendSegment(sentence, static_cast<int>(utf8ByteCount(sentence)));
            }
        } else {
            appendSegment(para, static_cast<int>(utf8ByteCount(para)));
        }
    }

    // 最后一个 chunk（前面仅在超限时封块，末尾剩余内容需显式提交）
    if (!current_chunk.empty()) {
        chunk_texts.push_back(current_chunk);
    }

    // 添加重叠（码点级切分，cut 天然在字符边界）。
    // 目的：跨块边界的语义片段在前一块结尾与后一块开头都能被检索命中；
    // 以 "\n---\n" 连接，重叠内容与后块正文之间保持可辨识的分隔。
    for (size_t i = 1; i < chunk_texts.size(); ++i) {
        if (chunk_texts[i - 1].empty()) continue;
        std::u32string overlap = overlapFromPrevious(chunk_texts[i - 1]);
        if (!overlap.empty()) {
            chunk_texts[i] = overlap + U"\n---\n" + chunk_texts[i];
        }
    }

    std::vector<TextChunk> chunks;
    chunks.reserve(chunk_texts.size());
    for (size_t i = 0; i < chunk_texts.size(); ++i) {
        // 块 id 为"章节id-序号"，序号即块序；文本统一还原为 UTF-8 后才可入库检索
        chunks.push_back(TextChunk::chapterChunk(source_id, static_cast<int>(i),
                                                 u32ToUtf8(chunk_texts[i])));
    }
    return chunks;
}

// ===========================================================================
// 辅助函数
// ===========================================================================

// 按段落边界把整章文本切成段落列表（单次扫描，O(n)）。
// 段落边界 = 换行（真实小说正文的 \n 仅出现在段落末尾，故单换行必为段界）；
// 连续换行（中间可含空白）合并为一个分隔符，不产生空段落。
// 输出约定：段落不含换行、去除首尾空白；整段仅空白时跳过。
std::vector<std::u32string> NovelChunker::splitParagraphs(const std::u32string& text)
{
    std::vector<std::u32string> paragraphs;

    // 段落边界：单个换行（一行一段）与空行（连续换行、中间可含空白字符）
    // 都是段落边界；连续换行序列合并为一个分隔符。
    // 真实小说正文的 \n 只出现在段落末尾（阅读器折行不落盘），故单换行必为段界。
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        // 跳过分隔符运行：\n 后尽可能多的 (\s* \n)
        if (text[i] == U'\n') {
            ++i;
            while (i < n && (text[i] == U' ' || text[i] == U'\t' || text[i] == U'\r'
                             || text[i] == U'\v' || text[i] == U'\f'))
                ++i;
            continue;
        }
        size_t start = i;
        while (i < n && text[i] != U'\n') ++i;  // 收集到下一个换行为止，得到段落原始区间 [start, end)
        size_t end = i;
        // 去除首尾空白：避免缩进/残留空格进入 chunk 浪费 token
        while (end > start && (text[end - 1] == U' ' || text[end - 1] == U'\t'
                               || text[end - 1] == U'\r'))
            --end;
        while (start < end && (text[start] == U' ' || text[start] == U'\t'
                               || text[start] == U'\r'))
            ++start;
        if (end > start) {
            // 整段仅空白（如只有空格的行）→ 跳过，不产出空段落
            paragraphs.push_back(text.substr(start, end - start));
        }
    }

    return paragraphs;
}

std::u32string NovelChunker::overlapFromPrevious(const std::u32string& prev_chunk_text) const
{
    if (prev_chunk_text.empty()) return {};

    // 重叠预算按 UTF-8 字节折算（与 chunk 尺寸口径一致），尾部截取按码点对齐
    const size_t overlap_bytes =
        static_cast<size_t>(utf8ByteCount(prev_chunk_text) * overlap_ratio_);
    if (overlap_bytes < 50) return {}; // 太短不添加重叠

    // 从上一块的末尾取约 overlap_bytes*2 字节，优先在句子边界处截断
    std::u32string tail = tailByBytes(prev_chunk_text, overlap_bytes * 2);
    const size_t boundary = lastSentenceEnd(tail);
    if (boundary != std::string::npos && boundary > tail.size() / 2) {
        return tail.substr(boundary);
    }

    // 未找到合适的句子边界 → 按预算取末尾后缀（码点级截取，天然在字符边界）
    return tailByBytes(prev_chunk_text, overlap_bytes);
}

// 按句末标点把文本切成句子列表（单次扫描，O(n)）。
// 切点位于句末标点（。！？…;.!?，见 isEndPunct）之后，标点跟随句子走不丢失；
// 输出句子去除首尾空白；若文本末尾无句末标点，残留部分也作为一个句子保留。
std::vector<std::u32string> NovelChunker::splitSentences(const std::u32string& text) const
{
    std::vector<std::u32string> sentences;

    size_t start = 0;  // 当前句子的起点（首个未切分的码点）
    for (size_t i = 0; i < text.size(); ++i) {
        if (!isEndPunct(text[i])) continue;  // 非句末标点：继续向后寻找切点
        ++i;  // 切点位于标点之后（标点保留在句内）
        std::u32string sentence = text.substr(start, i - start);
        // 去除首尾空白
        size_t b = 0, e = sentence.size();
        while (b < e && (sentence[b] == U' ' || sentence[b] == U'\t'
                         || sentence[b] == U'\r'))
            ++b;
        while (e > b && (sentence[e - 1] == U' ' || sentence[e - 1] == U'\t'
                         || sentence[e - 1] == U'\r'))
            --e;
        if (b < e) {  // 纯空白（如标点被空白包围）不产出空句子
            sentences.push_back(sentence.substr(b, e - b));
        }
        start = i;  // 下一句从标点之后续接（上一句的标点保持在句尾）
    }

    // 尾部无句末标点的残留文本（正文不总是以标点收尾，
    // 残留也须成句，否则超长段落按句切分时会丢掉这段内容）
    if (start < text.size()) {
        std::u32string tail = text.substr(start);
        size_t b = 0, e = tail.size();
        while (b < e && (tail[b] == U' ' || tail[b] == U'\t' || tail[b] == U'\r'))
            ++b;
        while (e > b && (tail[e - 1] == U' ' || tail[e - 1] == U'\t'
                         || tail[e - 1] == U'\r'))
            --e;
        if (b < e) {
            sentences.push_back(tail.substr(b, e - b));
        }
    }

    return sentences;
}

} // namespace retrieval
