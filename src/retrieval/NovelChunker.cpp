// NovelChunker 实现 — 小说文本智能切分。

#include "retrieval/NovelChunker.h"

#include "project/Models.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <regex>
#include <sstream>

using json = nlohmann::json;

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
    min_chunk_size_ = min_chunk_size;
    // 钳制下限：max ≤ 0 时硬切分支会无限递归（片段永不满足 ≤ max）
    max_chunk_size_ = std::max(1, max_chunk_size);
    overlap_ratio_ = std::clamp(overlap_ratio, 0.0, 0.3);
}

// ===========================================================================
// 章节切分
// ===========================================================================

std::vector<TextChunk> NovelChunker::chunkChapter(
    const Chapter& chapter,
    const std::string& markdown_content) const
{
    if (markdown_content.empty()) {
        spdlog::debug("[NovelChunker] 章节 {} 内容为空，跳过切分", chapter.id);
        return {};
    }

    // 统一按纯文本段落切分：不依赖 markdown 场景标记（真实小说正文无此结构）。
    spdlog::debug("[NovelChunker] 按段落边界切分章节 {} (纯文本)", chapter.id);
    return chunkByParagraphs(markdown_content, chapter.id);
}

// ===========================================================================
// 实体嵌入文本生成
// ===========================================================================

std::string NovelChunker::chunkCharacter(const Character& character)
{
    std::ostringstream ss;

    ss << "角色: " << character.name;
    if (!character.role.empty()) {
        ss << " (" << character.role << ")";
    }
    ss << "\n";

    if (!character.goal.empty()) {
        ss << "目标: " << character.goal << "\n";
    }
    if (!character.motivation.empty()) {
        ss << "动机: " << character.motivation << "\n";
    }
    if (!character.personality.empty()) {
        ss << "性格: " << character.personality << "\n";
    }
    if (!character.internal_conflict.empty()) {
        ss << "内在冲突: " << character.internal_conflict << "\n";
    }
    if (!character.external_conflict.empty()) {
        ss << "外在冲突: " << character.external_conflict << "\n";
    }
    if (!character.speaking_style.empty()) {
        ss << "说话风格: " << character.speaking_style << "\n";
    }
    if (!character.arc.empty()) {
        ss << "角色弧光: " << character.arc << "\n";
    }
    if (!character.traits.empty()) {
        ss << "特征: ";
        for (size_t i = 0; i < character.traits.size(); ++i) {
            if (i > 0) ss << "、";
            ss << character.traits[i];
        }
        ss << "\n";
    }
    if (!character.fear.empty()) {
        ss << "恐惧: " << character.fear << "\n";
    }
    if (!character.misbelief.empty()) {
        ss << "错误信念: " << character.misbelief << "\n";
    }

    return ss.str();
}

std::string NovelChunker::chunkSetting(const Setting& setting)
{
    std::ostringstream ss;

    ss << "设定: " << setting.name;
    if (!setting.category.empty()) {
        ss << " [" << setting.category << "]";
    }
    ss << "\n";

    if (!setting.description.empty()) {
        ss << "描述: " << setting.description << "\n";
    }
    if (!setting.story_function.empty()) {
        ss << "叙事功能: " << setting.story_function << "\n";
    }
    if (!setting.sensory_profile.empty()) {
        ss << "感官印象: " << setting.sensory_profile << "\n";
    }

    return ss.str();
}

std::string NovelChunker::chunkWorldRule(const WorldRule& rule)
{
    std::ostringstream ss;

    ss << "世界规则: " << rule.name << "\n";

    if (!rule.summary.empty()) {
        ss << "概要: " << rule.summary << "\n";
    }
    if (!rule.limitations.empty()) {
        ss << "限制: " << rule.limitations << "\n";
    }
    if (!rule.costs.empty()) {
        ss << "代价: " << rule.costs << "\n";
    }
    if (!rule.exceptions.empty()) {
        ss << "例外: " << rule.exceptions << "\n";
    }
    if (!rule.known_by.empty()) {
        ss << "知晓范围: " << rule.known_by << "\n";
    }

    return ss.str();
}

// ===========================================================================
// 切分策略实现
// ===========================================================================

std::vector<TextChunk> NovelChunker::chunkByParagraphs(
    const std::string& text,
    const std::string& source_id) const
{
    std::vector<TextChunk> chunks;
    auto paragraphs = splitParagraphs(text);

    std::string current_chunk;
    int current_size = 0;
    int chunk_index = 0;

    // 追加一个可嵌单元（段落或句子），达到上限且超过下限时封块。
    // 超长段（>max，多为无句末标点的长连续串，splitSentences 找不到切点）：
    // 按 UTF-8 字符边界硬切成 ≤max 的片段再逐个递归聚合，避免空块封不掉时
    // 产出超 max 的巨型块（设计规格"极端"回退）。
    std::function<void(const std::string&)> appendSegment = [&](const std::string& seg) {
        int seg_size = static_cast<int>(seg.size());
        if (seg_size > max_chunk_size_) {
            // 硬切：按 max 字节窗口切分，窗口末尾落在多字节字符中间（续字节 0x80-0xBF）
            // 时回退到最近字符边界（A11 思路，与 overlapFromPrevious 一致）
            size_t start = 0;
            while (start < seg.size()) {
                size_t cut = std::min(start + static_cast<size_t>(max_chunk_size_),
                                      seg.size());
                while (cut > start && (static_cast<unsigned char>(seg[cut]) & 0xC0) == 0x80)
                    --cut;
                // 极端：窗口内全是续字节时强制推进至少 1 字节，防止死循环
                if (cut == start)
                    cut = start + 1;
                appendSegment(seg.substr(start, cut - start));
                start = cut;
            }
            return;
        }
        if (current_size + seg_size > max_chunk_size_ && current_size >= min_chunk_size_) {
            chunks.push_back(TextChunk::chapterChunk(source_id, chunk_index++, current_chunk));
            current_chunk.clear();
            current_size = 0;
        }
        if (!current_chunk.empty()) {
            current_chunk += "\n\n";
            current_size += 2;
        }
        current_chunk += seg;
        current_size += seg_size;
    };

    for (const auto& para : paragraphs) {
        // 超长段落（如整章无空行时的"整章一段"）：按句子边界二次切分再聚合，
        // 避免单个 chunk 突破 max_chunk_size_，也避免从句子中间截断。
        if (static_cast<int>(para.size()) > max_chunk_size_) {
            for (const auto& sentence : splitSentences(para)) {
                appendSegment(sentence);
            }
        } else {
            appendSegment(para);
        }
    }

    // 最后一个 chunk
    if (!current_chunk.empty()) {
        chunks.push_back(TextChunk::chapterChunk(source_id, chunk_index++, current_chunk));
    }

    // 添加重叠
    for (size_t i = 1; i < chunks.size(); ++i) {
        if (!chunks[i - 1].text.empty()) {
            std::string overlap = overlapFromPrevious(chunks[i - 1].text);
            if (!overlap.empty()) {
                chunks[i].text = overlap + "\n---\n" + chunks[i].text;
            }
        }
    }

    return chunks;
}

// ===========================================================================
// 辅助函数
// ===========================================================================

std::vector<std::string> NovelChunker::splitParagraphs(const std::string& text)
{
    std::vector<std::string> paragraphs;

    // 按双换行（空行）切分
    std::regex blank_line(R"(\n\s*\n)");

    std::sregex_token_iterator it(text.begin(), text.end(), blank_line, -1);
    std::sregex_token_iterator end;

    for (; it != end; ++it) {
        std::string para = it->str();
        // 去除首尾空白
        size_t start = para.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) continue;

        size_t finish = para.find_last_not_of(" \t\n\r");
        para = para.substr(start, finish - start + 1);

        if (!para.empty()) {
            paragraphs.push_back(para);
        }
    }

    return paragraphs;
}

std::string NovelChunker::overlapFromPrevious(const std::string& prev_chunk_text) const
{
    if (prev_chunk_text.empty()) return {};

    int overlap_size = static_cast<int>(prev_chunk_text.size() * overlap_ratio_);
    if (overlap_size < 50) return {}; // 太短不添加重叠

    // 从上一块的末尾取 overlap_size 字符
    // 在句子边界处截断
    std::string tail = prev_chunk_text.substr(
        prev_chunk_text.size() - std::min(overlap_size * 2,
                                          static_cast<int>(prev_chunk_text.size())));

    auto last_period = tail.find_last_of(".。！？!?\n");
    if (last_period != std::string::npos && last_period > tail.size() / 2) {
        return tail.substr(last_period + 1);
    }

    // 未找到合适的句子边界 → 回退字节截断，但确保在 UTF-8 字符边界
    // （A11 修复：避免在多字节字符中间截断产生乱码）
    size_t cut = prev_chunk_text.size() - overlap_size;
    while (cut > 0 && (static_cast<unsigned char>(prev_chunk_text[cut]) & 0xC0) == 0x80)
        --cut;
    return prev_chunk_text.substr(cut);
}

std::vector<std::string> NovelChunker::splitSentences(const std::string& text) const
{
    std::vector<std::string> sentences;

    // 句末标点候选（按其 UTF-8 编码字节序列匹配）：
    // 中文 。(E3 80 82) ！(EF BC 81) ？(EF BC 9F) …(E2 80 A6)；英文 . ! ?
    static const std::vector<std::string> kEndPuncts = {
        "\xE3\x80\x82",  // 。
        "\xEF\xBC\x81",  // ！
        "\xEF\xBC\x9F",  // ？
        "\xE2\x80\xA6",  // …
        ".", "!", "?"
    };

    size_t start = 0;
    for (size_t i = 0; i < text.size();) {
        bool is_end = false;
        const size_t remain = text.size() - i;
        for (const auto& p : kEndPuncts) {
            if (remain >= p.size() && text.compare(i, p.size(), p) == 0) {
                is_end = true;
                i += p.size();  // 跳过标点本身，切点位于标点之后
                break;
            }
        }
        if (!is_end) {
            ++i;  // 逐字节前进；非标点字节（含多字节字符的部分字节）不作为切点
            continue;
        }

        std::string sentence = text.substr(start, i - start);
        // 去除首尾空白
        size_t b = sentence.find_first_not_of(" \t\n\r");
        size_t e = sentence.find_last_not_of(" \t\n\r");
        if (b != std::string::npos) {
            sentences.push_back(sentence.substr(b, e - b + 1));
        }
        start = i;
    }

    // 尾部无句末标点的残留文本
    if (start < text.size()) {
        std::string tail = text.substr(start);
        size_t b = tail.find_first_not_of(" \t\n\r");
        size_t e = tail.find_last_not_of(" \t\n\r");
        if (b != std::string::npos) {
            sentences.push_back(tail.substr(b, e - b + 1));
        }
    }

    return sentences;
}

} // namespace retrieval
