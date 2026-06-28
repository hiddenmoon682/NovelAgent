/// NovelChunker 实现 — 小说文本智能切分。

#include "retrieval/NovelChunker.h"

#include "project/Models.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
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
    max_chunk_size_ = max_chunk_size;
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

    // 策略 1: 按 Scene 边界切分
    if (!chapter.scenes.empty()) {
        spdlog::debug("[NovelChunker] 按 Scene 边界切分章节 {} ({} 个场景)",
                      chapter.id, chapter.scenes.size());
        return chunkByScenes(markdown_content, chapter);
    }

    // 策略 2: 退化为按段落边界切分
    spdlog::debug("[NovelChunker] 按段落边界切分章节 {} (无 Scene 信息)", chapter.id);
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

std::vector<TextChunk> NovelChunker::chunkByScenes(
    const std::string& markdown_content,
    const Chapter& chapter) const
{
    std::vector<TextChunk> chunks;

    // 尝试在正文中查找 Scene 标记
    // 常见格式: "## Scene N: ...", "### 场景N: ...", "**场景 N**"
    std::regex scene_header(
        R"(#{2,3}\s*(?:Scene|场景)\s*\d*[:：]?\s*([^\n]*)\n)"
    );

    std::sregex_iterator it(markdown_content.begin(), markdown_content.end(), scene_header);
    std::sregex_iterator end;

    if (it == end) {
        // 正文中没有 Scene 标记，回退到段落切分
        return chunkByParagraphs(markdown_content, chapter.id);
    }

    // 按 Scene 标记切分正文
    size_t last_pos = 0;
    int chunk_index = 0;

    for (; it != end; ++it) {
        size_t match_start = it->position();
        std::string scene_text = markdown_content.substr(
            last_pos, match_start - last_pos);

        // 跳过第一个空块（Scene 标记前的章节概要）
        if (!scene_text.empty() && last_pos > 0) {
            // 对单段过长的 scene 做二次切分
            if (static_cast<int>(scene_text.size()) > max_chunk_size_) {
                auto sub_chunks = chunkByParagraphs(scene_text,
                    chapter.id + "-s" + std::to_string(chunk_index));
                for (auto& sub : sub_chunks) {
                    sub.id = chapter.id + "-" + std::to_string(chunk_index++);
                    chunks.push_back(std::move(sub));
                }
            } else {
                chunks.push_back(TextChunk::chapterChunk(chapter.id, chunk_index++, scene_text));
            }
        }
        last_pos = match_start;
    }

    // 最后一个 Scene 之后的内容
    if (last_pos < markdown_content.size()) {
        std::string remaining = markdown_content.substr(last_pos);
        if (!remaining.empty()) {
            auto sub_chunks = chunkByParagraphs(remaining,
                chapter.id + "-s" + std::to_string(chunk_index));
            for (auto& sub : sub_chunks) {
                sub.id = chapter.id + "-" + std::to_string(chunk_index++);
                chunks.push_back(std::move(sub));
            }
        }
    }

    // 添加重叠
    for (size_t i = 1; i < chunks.size(); ++i) {
        if (!chunks[i - 1].text.empty()) {
            std::string overlap = overlapFromPrevious(chunks[i - 1].text);
            if (!overlap.empty()) {
                chunks[i].text = overlap + "\n" + chunks[i].text;
            }
        }
    }

    return chunks;
}

std::vector<TextChunk> NovelChunker::chunkByParagraphs(
    const std::string& text,
    const std::string& source_id) const
{
    std::vector<TextChunk> chunks;
    auto paragraphs = splitParagraphs(text);

    std::string current_chunk;
    int current_size = 0;
    int chunk_index = 0;

    for (const auto& para : paragraphs) {
        int para_size = static_cast<int>(para.size());

        // 如果当前 chunk 加上这个段落会超出 max_chunk_size_
        if (current_size + para_size > max_chunk_size_ && current_size >= min_chunk_size_) {
            // 保存当前 chunk
            chunks.push_back(TextChunk::chapterChunk(source_id, chunk_index++, current_chunk));
            current_chunk.clear();
            current_size = 0;
        }

        if (!current_chunk.empty()) {
            current_chunk += "\n\n";
            current_size += 2;
        }
        current_chunk += para;
        current_size += para_size;
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

} // namespace retrieval
