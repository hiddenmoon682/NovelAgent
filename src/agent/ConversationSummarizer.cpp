/// ConversationSummarizer 实现 — 规则提取对话关键信息。

#include "agent/ConversationSummarizer.h"

#include "llm/Message.h"

#include <algorithm>
#include <regex>
#include <set>
#include <sstream>

namespace agent {

namespace {
const std::regex kChapterIdPattern(R"(ch-\d{3,})");
const std::regex kChineseChapterPattern(R"(第[一二三四五六七八九十百千\d]+章)");
const std::regex kQuotedNamePattern(R"(["'""]([^"'"".。，,]+)["'"”])");

bool containsKeyword(const std::string& text, const std::vector<std::string>& keywords) {
    for (const auto& kw : keywords) {
        if (text.find(kw) != std::string::npos) return true;
    }
    return false;
}
} // namespace

ConversationSummary ConversationSummarizer::summarize(
    const std::vector<llm::Message>& messages) const
{
    ConversationSummary summary;
    if (messages.empty()) return summary;

    summary.source_message_count = static_cast<int>(messages.size());
    summary.character_names = extractCharacterNames(messages);
    summary.chapter_refs = extractChapterRefs(messages);
    summary.plot_points = extractPlotPoints(messages);
    summary.tasks = extractTasks(messages);

    std::ostringstream ss;
    if (!summary.tasks.empty()) {
        ss << "当前任务: ";
        for (size_t i = 0; i < summary.tasks.size(); ++i) {
            if (i > 0) ss << "; ";
            ss << summary.tasks[i];
        }
        ss << "。";
    }
    if (!summary.character_names.empty()) {
        if (!ss.str().empty()) ss << " ";
        ss << "提及角色: ";
        for (size_t i = 0; i < summary.character_names.size() && i < 5; ++i) {
            if (i > 0) ss << "、";
            ss << summary.character_names[i];
        }
        ss << "。";
    }
    if (!summary.plot_points.empty()) {
        if (!ss.str().empty()) ss << " ";
        ss << "剧情要点: ";
        for (size_t i = 0; i < summary.plot_points.size(); ++i) {
            if (i > 0) ss << " ";
            ss << "[*] " << summary.plot_points[i];
        }
    }
    if (!summary.chapter_refs.empty()) {
        if (!ss.str().empty()) ss << " ";
        ss << "涉及章节: ";
        for (size_t i = 0; i < summary.chapter_refs.size() && i < 5; ++i) {
            if (i > 0) ss << "、";
            ss << summary.chapter_refs[i];
        }
        ss << "。";
    }
    summary.summary = ss.str();
    return summary;
}

std::string ConversationSummarizer::render(const ConversationSummary& summary) {
    return summary.summary;
}

// ── 私有方法 ──

std::vector<std::string> ConversationSummarizer::splitSentences(const std::string& text) {
    std::vector<std::string> sentences;
    if (text.empty()) return sentences;

    static const std::regex sentence_end(R"([。？！…]|[.!?](?=\s|$|\n))");
    std::sregex_iterator it(text.begin(), text.end(), sentence_end);
    std::sregex_iterator end;
    size_t last_pos = 0;

    for (; it != end; ++it) {
        size_t match_end = it->position() + it->length();
        std::string sentence = text.substr(last_pos, match_end - last_pos);
        size_t start = sentence.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            sentence = sentence.substr(start);
            if (!sentence.empty()) sentences.push_back(sentence);
        }
        last_pos = match_end;
    }
    if (last_pos < text.size()) {
        std::string remaining = text.substr(last_pos);
        size_t start = remaining.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            remaining = remaining.substr(start);
            if (!remaining.empty()) sentences.push_back(remaining);
        }
    }
    return sentences;
}

std::vector<std::string> ConversationSummarizer::extractCharacterNames(
    const std::vector<llm::Message>& messages)
{
    std::set<std::string> names;
    for (const auto& msg : messages) {
        const std::string& text = msg.content;
        auto words_begin = std::sregex_iterator(text.begin(), text.end(), kQuotedNamePattern);
        auto words_end = std::sregex_iterator();
        for (auto it = words_begin; it != words_end; ++it) {
            std::string name = (*it)[1].str();
            if (name.size() >= 2 && name.size() <= 5 &&
                std::none_of(name.begin(), name.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) ||
                           c == ' ' || c == '\n' || c == '\t';
                })) {
                names.insert(name);
            }
        }
        std::regex char_kw(R"((角色|人物|主角|配角|反派)[：:]\s*(\S+))");
        auto kw_begin = std::sregex_iterator(text.begin(), text.end(), char_kw);
        auto kw_end = std::sregex_iterator();
        for (auto it = kw_begin; it != kw_end; ++it) {
            names.insert((*it)[2].str());
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> ConversationSummarizer::extractChapterRefs(
    const std::vector<llm::Message>& messages)
{
    std::set<std::string> refs;
    for (const auto& msg : messages) {
        const std::string& text = msg.content;
        auto id_begin = std::sregex_iterator(text.begin(), text.end(), kChapterIdPattern);
        auto id_end = std::sregex_iterator();
        for (auto it = id_begin; it != id_end; ++it) refs.insert(it->str());
        auto ch_begin = std::sregex_iterator(text.begin(), text.end(), kChineseChapterPattern);
        auto ch_end = std::sregex_iterator();
        for (auto it = ch_begin; it != ch_end; ++it) refs.insert(it->str());
    }
    return std::vector<std::string>(refs.begin(), refs.end());
}

std::vector<std::string> ConversationSummarizer::extractPlotPoints(
    const std::vector<llm::Message>& messages) const
{
    std::vector<std::string> points;
    for (const auto& msg : messages) {
        if (msg.role != llm::MessageRole::User &&
            msg.role != llm::MessageRole::Assistant) continue;
        auto sentences = splitSentences(msg.content);
        for (const auto& sent : sentences) {
            if (containsKeyword(sent, keywords_.plot_keywords)) {
                std::string trimmed = sent.size() > 80 ? sent.substr(0, 80) + "..." : sent;
                points.push_back(trimmed);
                if (static_cast<int>(points.size()) >= keywords_.max_plot_points) break;
            }
        }
        if (static_cast<int>(points.size()) >= keywords_.max_plot_points) break;
    }
    std::set<std::string> unique(points.begin(), points.end());
    return std::vector<std::string>(unique.begin(), unique.end());
}

std::vector<std::string> ConversationSummarizer::extractTasks(
    const std::vector<llm::Message>& messages) const
{
    std::vector<std::string> tasks;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != llm::MessageRole::User) continue;
        auto sentences = splitSentences(it->content);
        for (const auto& sent : sentences) {
            if (containsKeyword(sent, keywords_.task_keywords)) {
                tasks.push_back(sent.size() > 100 ? sent.substr(0, 100) + "..." : sent);
                if (static_cast<int>(tasks.size()) >= keywords_.max_tasks) break;
            }
        }
        if (!tasks.empty()) break;
    }
    return tasks;
}

} // namespace agent
