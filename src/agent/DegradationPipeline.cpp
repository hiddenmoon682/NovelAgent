/// DegradationPipeline 实现 — 策略模式降级管线。

#include "agent/DegradationPipeline.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <regex>

namespace agent {

// ===========================================================================
// 策略注册与执行
// ===========================================================================

void DegradationPipeline::registerDefaultStrategies()
{
    strategies_.clear();
    registerStrategy(std::make_unique<TruncateChapterStrategy>());
    registerStrategy(std::make_unique<RemoveCharacterDetailsStrategy>());
    registerStrategy(std::make_unique<RemoveAdjacentChaptersStrategy>());
    registerStrategy(std::make_unique<TruncateConversationStrategy>());
    registerStrategy(std::make_unique<SummarizeStrategy>());

    // 按 level 排序，确保按 L1→L5 顺序执行
    std::sort(strategies_.begin(), strategies_.end(),
        [](const auto& a, const auto& b) {
            return static_cast<int>(a->level()) < static_cast<int>(b->level());
        });
}

void DegradationPipeline::registerStrategy(std::unique_ptr<IDegradationStrategy> strategy)
{
    // 替换同等级已有策略
    auto it = std::find_if(strategies_.begin(), strategies_.end(),
        [&](const auto& s) { return s->level() == strategy->level(); });
    if (it != strategies_.end()) {
        *it = std::move(strategy);
    } else {
        strategies_.push_back(std::move(strategy));
    }
}

DegradationLevel DegradationPipeline::determineLevel(
    int required_tokens, int available_budget) const
{
    if (required_tokens <= available_budget) {
        return DegradationLevel::None;
    }

    double ratio = static_cast<double>(required_tokens) / available_budget;

    // 从最轻策略开始，找到第一个能覆盖超出比例的
    for (const auto& s : strategies_) {
        if (ratio <= 1.0 + s->estimatedSavingRatio()) {
            return s->level();
        }
    }

    // 回退到最重策略
    return DegradationLevel::Summarize;
}

std::string DegradationPipeline::execute(
    const std::string& prompt, DegradationLevel level) const
{
    if (level == DegradationLevel::None || prompt.empty()) {
        return prompt;
    }

    std::string result = prompt;

    // 依次应用所有 ≤ target_level 的策略
    for (const auto& s : strategies_) {
        if (static_cast<int>(s->level()) <= static_cast<int>(level)) {
            result = s->apply(result);
            spdlog::debug("[DegradationPipeline] L{} 降级 (结果 {} 字符)",
                          static_cast<int>(s->level()), result.size());
        }
    }

    return result;
}

// ===========================================================================
// L1 — 截断章节内容
// ===========================================================================

std::string TruncateChapterStrategy::apply(const std::string& prompt)
{
    if (prompt.size() <= 4000) return prompt;

    std::string result = prompt;
    size_t head_end = result.find("## 当前章节");
    if (head_end == std::string::npos) head_end = result.find("# 章节");

    if (head_end != std::string::npos && head_end < result.size() / 2) {
        std::string head = result.substr(0, head_end);
        std::string tail = result.substr(
            result.size() > 4000 ? result.size() - 3000 : head_end);
        result = head + "\n[章节内容已截断到末尾 ~2000 字]\n" + tail;
    } else {
        result = result.substr(result.size() > 3000 ? result.size() - 3000 : 0);
        result = "[内容已截断]\n" + result;
    }
    return result;
}

// ===========================================================================
// L2 — 移除角色详细档案
// ===========================================================================

std::string RemoveCharacterDetailsStrategy::apply(const std::string& prompt)
{
    std::regex char_detail(R"(### 角色:[\s\S]*?(?=###|\Z))");
    std::string simplified;
    std::sregex_iterator it(prompt.begin(), prompt.end(), char_detail);
    std::sregex_iterator end;
    size_t last_pos = 0;

    for (; it != end; ++it) {
        simplified += prompt.substr(last_pos, it->position() - last_pos);

        std::string block = it->str();
        std::regex name_re(R"(\*\*姓名\*\*:\s*(\S+))");
        std::regex role_re(R"(\*\*角色类型\*\*:\s*(\S+))");
        std::smatch name_match, role_match;

        std::string name = std::regex_search(block, name_match, name_re)
            ? name_match[1].str() : "未知";
        std::string role = std::regex_search(block, role_match, role_re)
            ? role_match[1].str() : "";

        simplified += "### 角色: " + name;
        if (!role.empty()) simplified += " (" + role + ")";
        simplified += "\n  [详细档案已压缩，使用 get_character 工具查询]\n";
        last_pos = it->position() + it->length();
    }
    simplified += prompt.substr(last_pos);
    return simplified.empty() ? prompt : simplified;
}

// ===========================================================================
// L3 — 移除相邻章节大纲
// ===========================================================================

std::string RemoveAdjacentChaptersStrategy::apply(const std::string& prompt)
{
    std::regex adjacent(R"(### 相邻章节[\s\S]*?(?=###|\Z))");
    return std::regex_replace(prompt, adjacent, "[相邻章节大纲已移除]\n");
}

// ===========================================================================
// L4 — 标记对话截断（实际截断在消息层处理）
// ===========================================================================

std::string TruncateConversationStrategy::apply(const std::string& prompt)
{
    // L4 实际工作在 truncateMessages 中做，此处仅做标记
    return prompt;
}

// ===========================================================================
// L5 — 全文压缩为摘要
// ===========================================================================

std::string SummarizeStrategy::apply(const std::string& prompt)
{
    std::string compressed;
    std::regex title_re(R"(# 项目:\s*(\S+))");
    std::smatch title_match;
    if (std::regex_search(prompt, title_match, title_re)) {
        compressed += "# 项目: " + title_match[1].str() + "\n";
    }
    compressed += "[上下文已完全压缩 — token 预算严重不足]\n";
    compressed += "请使用工具（get_character、read_chapter 等）按需查询信息。\n";

    std::regex task_re(R"(当前任务[：:]\s*([^\n]+))");
    std::smatch task_match;
    if (std::regex_search(prompt, task_match, task_re)) {
        compressed += "当前任务: " + task_match[1].str() + "\n";
    }
    return compressed;
}

} // namespace agent
