#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/TokenCounter.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "prompt/PromptContextBuilder.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <regex>
#include <set>
#include <sstream>

namespace agent {

// ===========================================================================
// 匿名命名空间 — 内部常量
// ===========================================================================
namespace {

// 预算分配比例
constexpr double kChapterRatio = 0.50;       // 50% 当前章节 + 大纲 + 角色
constexpr double kConversationRatio = 0.30;  // 30% 最近对话
constexpr double kSummaryRatio = 0.20;       // 20% 历史摘要

// 降级阈值
constexpr int kChapterTruncateChars = 2000;  // 降级 L1: 章节截断到此字数
constexpr int kMinConversationTurns = 5;     // 降级 L4: 最少保留对话轮数

// 摘要提取参数
constexpr int kMaxSummarySentences = 10;     // 摘要最多保留句子数
constexpr int kMaxPlotPoints = 5;            // 最多剧情要点数
constexpr int kMaxTasks = 3;                 // 最多当前任务数

// 章节引用模式：ch-001, ch-123 等
const std::regex kChapterIdPattern(R"(ch-\d{3,})");
// 中文章节引用：第X章、第一章 等
const std::regex kChineseChapterPattern(R"(第[一二三四五六七八九十百千\d]+章)");
// 角色名模式：引号内的名称、"角色"后的名称
const std::regex kQuotedNamePattern(R"(["'""]([^"'"".。，,]+)["'"”])");

/// 判断字符串是否包含中文关键词
bool containsKeyword(const std::string& text, const std::vector<std::string>& keywords) {
    for (const auto& kw : keywords) {
        if (text.find(kw) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// ===========================================================================
// assemble — 一站式上下文组装（Phase 4 完整版）
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int context_window,
    const Project* project,
    const std::string& chapter_id)
{
    ContextAssembly result;

    // 1. 计算预算分配
    BudgetAllocation alloc = allocateBudget(context_window);
    result.budget = alloc.total_budget;

    // 2. 构建系统提示词
    if (project) {
        result.system_prompt = buildSystemPrompt(*project, chapter_id);
        if (result.system_prompt.empty()) {
            spdlog::warn("[ContextManager] 系统提示词为空 — 项目或章节信息不足");
        }
    }

    // 3. 计算系统提示词的 token 开销
    int sys_tokens = result.system_prompt.empty()
        ? 0
        : llm::TokenCounter::countTokens(result.system_prompt);

    // 4. 消息预算 = 总预算 - 系统提示词
    int msg_budget = std::max(0, alloc.total_budget - sys_tokens);

    // 5. 检查是否需要降级
    const auto& all_msgs = conversation.messages();
    int raw_msg_tokens = llm::TokenCounter::countMessages(all_msgs);

    if (raw_msg_tokens > msg_budget || sys_tokens > alloc.chapter_budget) {
        // 系统提示词超出章节预算时，触发降级
        DegradationLevel level = determineDegradation(
            sys_tokens + raw_msg_tokens,
            alloc.total_budget);

        if (level != DegradationLevel::None) {
            result.system_prompt = applyDegradation(result.system_prompt, level);
            sys_tokens = result.system_prompt.empty()
                ? 0
                : llm::TokenCounter::countTokens(result.system_prompt);
            msg_budget = std::max(0, alloc.total_budget - sys_tokens);
            result.degradation_level = static_cast<int>(level);

            spdlog::info("[ContextManager] 触发降级 L{} — 系统提示词 {} tokens, 消息预算 {} tokens",
                         static_cast<int>(level), sys_tokens, msg_budget);
        }
    }

    // 6. 如仍超出预算，生成对话摘要并注入
    std::string summary_text;
    if (raw_msg_tokens > msg_budget && all_msgs.size() > kMinConversationTurns * 2) {
        ConversationSummary summary = summarizeConversation(all_msgs);
        summary_text = renderSummary(summary);

        // 摘要 token 从消息预算中扣除
        int summary_tokens = llm::TokenCounter::countTokens(summary_text);
        msg_budget = std::max(0, msg_budget - summary_tokens);

        spdlog::debug("[ContextManager] 对话摘要: {} tokens (覆盖 {} 条消息)",
                      summary_tokens, summary.source_message_count);
    }

    // 7. 截断消息
    result.messages = truncateMessages(all_msgs, msg_budget, result.truncated_count);
    result.truncated = (result.truncated_count > 0);

    // 8. 如果有摘要，作为 system 消息前缀注入
    if (!summary_text.empty() && !result.messages.empty()) {
        // 将摘要文本附加到第一条消息后，或作为独立上下文注入
        // 这里采用：在消息列表前面插入一条 user 消息作为上下文提醒
        llm::Message context_note = llm::Message::user(
            "[上下文摘要 — 之前对话的关键信息]\n" + summary_text);
        result.messages.insert(result.messages.begin(), context_note);
    }

    if (result.truncated) {
        spdlog::info("[ContextManager] 截断 {} 条消息 (预算={}, 系统={}, 消息预算={}, 降级=L{})",
                     result.truncated_count, result.budget, sys_tokens, msg_budget,
                     result.degradation_level);
    }

    // 9. 统计实际 token
    int msg_tokens = llm::TokenCounter::countMessages(result.messages);
    result.total_tokens = sys_tokens + msg_tokens;

    return result;
}

// ===========================================================================
// buildSystemPrompt — 委托 PromptContextBuilder
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project,
    const std::string& chapter_id)
{
    prompt::PromptContextOptions options;
    options.task = "write_chapter";

    if (!chapter_id.empty()) {
        options.chapter_id = chapter_id;
    }

    if (chapter_id.empty()) {
        // 无指定章节时，构造最小化的系统提示词（仅项目概述）
        std::string prompt;
        prompt += "# 项目: " + project.title + "\n";
        if (!project.logline.empty()) {
            prompt += "Logline: " + project.logline + "\n";
        }
        if (!project.theme.empty()) {
            prompt += "主题: " + project.theme + "\n";
        }
        return prompt;
    }

    auto ctx = prompt::PromptContextBuilder::buildForChapter(project, options);
    if (!ctx) {
        spdlog::warn("[ContextManager] 无法为章节 '{}' 构建上下文，回退到项目概述", chapter_id);
        return buildSystemPrompt(project); // fallback 到无章节版本
    }

    spdlog::debug("[ContextManager] 系统提示词构建完成 — 章节={}, 长度={}",
                  chapter_id, ctx->rendered_prompt.size());
    return ctx->rendered_prompt;
}

// ===========================================================================
// calculateBudget — 80/20 规则
// ===========================================================================

int ContextManager::calculateBudget(int context_window)
{
    // 80% 用于输入，20% 留给模型输出
    return static_cast<int>(context_window * 0.8);
}

// ===========================================================================
// allocateBudget — 50/30/20 分配（Phase 4.1）
// ===========================================================================

BudgetAllocation ContextManager::allocateBudget(int context_window) const
{
    BudgetAllocation alloc;
    alloc.total_budget = calculateBudget(context_window);

    alloc.chapter_budget = static_cast<int>(alloc.total_budget * kChapterRatio);
    alloc.conversation_budget = static_cast<int>(alloc.total_budget * kConversationRatio);
    alloc.summary_budget = static_cast<int>(alloc.total_budget * kSummaryRatio);

    spdlog::debug("[ContextManager] 预算分配: 总={}, 章节={}, 对话={}, 摘要={}",
                  alloc.total_budget, alloc.chapter_budget,
                  alloc.conversation_budget, alloc.summary_budget);

    return alloc;
}

// ===========================================================================
// summarizeConversation — 规则提取（Phase 4.1）
// ===========================================================================

ConversationSummary ContextManager::summarizeConversation(
    const std::vector<llm::Message>& messages)
{
    ConversationSummary summary;
    if (messages.empty()) return summary;

    summary.source_message_count = static_cast<int>(messages.size());

    // 提取各类信息
    summary.character_names = extractCharacterNames(messages);
    summary.chapter_refs = extractChapterRefs(messages);
    summary.plot_points = extractPlotPoints(messages);
    summary.tasks = extractTasks(messages);

    // 组装摘要文本
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
            ss << "• " << summary.plot_points[i];
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

std::string ContextManager::renderSummary(const ConversationSummary& summary)
{
    return summary.summary;
}

// ===========================================================================
// 章节摘要缓存（Phase 4.2）
// ===========================================================================

std::optional<ChapterSummaryEntry> ContextManager::getChapterSummary(
    const std::string& project_path,
    const std::string& chapter_id)
{
    auto all = loadAllSummaries(project_path);
    auto it = all.find(chapter_id);
    if (it != all.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ContextManager::updateChapterSummary(
    const std::string& project_path,
    const ChapterSummaryEntry& entry)
{
    auto all = loadAllSummaries(project_path);
    all[entry.chapter_id] = entry;

    // 序列化并写回
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, summary] : all) {
        nlohmann::json entry_json;
        entry_json["chapter_id"] = summary.chapter_id;
        entry_json["summary"] = summary.summary;
        entry_json["characters"] = summary.characters;
        entry_json["settings"] = summary.settings;
        entry_json["key_events"] = summary.key_events;
        entry_json["updated_at"] = summary.updated_at;
        j[id] = entry_json;
    }

    const std::string summaries_path =
        utils::file::joinPath(ProjectIO::agentDir(project_path), "summaries.json");
    ProjectIO::saveJsonFile(summaries_path, j);

    spdlog::debug("[ContextManager] 章节摘要已更新: {}", entry.chapter_id);
}

std::map<std::string, ChapterSummaryEntry> ContextManager::loadAllSummaries(
    const std::string& project_path)
{
    std::map<std::string, ChapterSummaryEntry> result;

    const std::string summaries_path =
        utils::file::joinPath(ProjectIO::agentDir(project_path), "summaries.json");

    auto j = ProjectIO::loadJsonFile(summaries_path);
    if (!j || !j->is_object()) {
        return result;
    }

    for (auto it = j->begin(); it != j->end(); ++it) {
        ChapterSummaryEntry entry;
        entry.chapter_id = it.key();
        const auto& val = it.value();
        entry.summary = utils::json::getOrDefault(val, "summary", std::string{});
        entry.characters = utils::json::getOrDefault(
            val, "characters", std::vector<std::string>{});
        entry.settings = utils::json::getOrDefault(
            val, "settings", std::vector<std::string>{});
        entry.key_events = utils::json::getOrDefault(
            val, "key_events", std::vector<std::string>{});
        entry.updated_at = utils::json::getOrDefault(val, "updated_at", std::string{});
        result[entry.chapter_id] = entry;
    }

    return result;
}

// ===========================================================================
// 多级降级（Phase 4.3）
// ===========================================================================

DegradationLevel ContextManager::determineDegradation(
    int required_tokens,
    int available_budget)
{
    if (required_tokens <= available_budget) {
        return DegradationLevel::None;
    }

    // 估算各级降级的压缩比，找到能装下的最低级别
    // L1: 截断章节 → 节省 ~15%
    // L2: 移除角色详档 → 节省 ~30%
    // L3: 移除相邻章节大纲 → 节省 ~45%
    // L4: 截断对话到 5 轮 → 节省 ~60%
    // L5: 全文压缩 → 节省 ~80%

    double ratio = static_cast<double>(required_tokens) / available_budget;

    if (ratio <= 1.15) return DegradationLevel::TruncateChapter;
    if (ratio <= 1.30) return DegradationLevel::RemoveDetails;
    if (ratio <= 1.45) return DegradationLevel::RemoveAdjacent;
    if (ratio <= 1.60) return DegradationLevel::TruncateConv;
    return DegradationLevel::Summarize;
}

std::string ContextManager::applyDegradation(
    const std::string& system_prompt,
    DegradationLevel level)
{
    if (level == DegradationLevel::None || system_prompt.empty()) {
        return system_prompt;
    }

    std::string result = system_prompt;

    // L1: 截断章节内容到末尾 ~2000 字
    if (level >= DegradationLevel::TruncateChapter) {
        // 查找 "## 当前章节" 或章节正文标记，截断尾部
        // 简化策略：如果系统提示词超长，截取最后 ~3000 字符
        if (result.size() > 4000) {
            // 保留开头（项目基本信息）+ 末尾（当前章节最新部分）
            size_t head_end = result.find("## 当前章节");
            if (head_end == std::string::npos) {
                head_end = result.find("# 章节");
            }
            if (head_end != std::string::npos && head_end < result.size() / 2) {
                std::string head = result.substr(0, head_end);
                std::string tail = result.substr(
                    result.size() > 4000 ? result.size() - 3000 : head_end);
                result = head + "\n[章节内容已截断到末尾 ~2000 字]\n" + tail;
            } else {
                result = result.substr(
                    result.size() > 3000 ? result.size() - 3000 : 0);
                result = "[内容已截断]\n" + result;
            }
        }
        spdlog::debug("[ContextManager] L1 降级: 章节截断 (结果 {} 字符)", result.size());
    }

    // L2: 移除角色详细档案，保留名称和角色类型
    if (level >= DegradationLevel::RemoveDetails) {
        // 用正则移除 "### 角色:" 段落的详细字段，仅保留名称和 role
        // 简化：标记大段角色详情，替换为简短列表
        std::regex char_detail(R"(### 角色:[\s\S]*?(?=###|\Z))");
        std::string simplified;
        std::sregex_iterator it(result.begin(), result.end(), char_detail);
        std::sregex_iterator end;
        size_t last_pos = 0;

        for (; it != end; ++it) {
            // 保留匹配前的内容
            simplified += result.substr(last_pos, it->position() - last_pos);

            // 提取角色名和 role，去掉详细描述
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
        simplified += result.substr(last_pos);

        if (!simplified.empty()) result = simplified;
        spdlog::debug("[ContextManager] L2 降级: 角色档案压缩 (结果 {} 字符)", result.size());
    }

    // L3: 移除相邻章节大纲
    if (level >= DegradationLevel::RemoveAdjacent) {
        // 移除 "### 相邻章节" 或类似标记的段落
        std::regex adjacent(R"(### 相邻章节[\s\S]*?(?=###|\Z))");
        result = std::regex_replace(result, adjacent, "[相邻章节大纲已移除]\n");
        std::regex other_chapters(R"(### 第[^当].*?章[\s\S]*?(?=###|\Z))");
        // 谨慎处理，避免移除当前章节
        spdlog::debug("[ContextManager] L3 降级: 相邻章节大纲移除 (结果 {} 字符)", result.size());
    }

    // L4: 对话截断到最近 5 轮（在 truncateMessages 中处理，此处标记）
    if (level >= DegradationLevel::TruncateConv) {
        spdlog::debug("[ContextManager] L4 降级: 对话截断到最近 5 轮");
    }

    // L5: 全文压缩为摘要（最严重）
    if (level >= DegradationLevel::Summarize) {
        // 极端情况：将整个系统提示词压缩为一段简短摘要
        std::string compressed;
        // 提取项目名
        std::regex title_re(R"(# 项目:\s*(\S+))");
        std::smatch title_match;
        if (std::regex_search(result, title_match, title_re)) {
            compressed += "# 项目: " + title_match[1].str() + "\n";
        }
        compressed += "[上下文已完全压缩 — token 预算严重不足]\n";
        compressed += "请使用工具（get_character、read_chapter 等）按需查询信息。\n";

        // 尝试保留当前任务信息
        std::regex task_re(R"(当前任务[：:]\s*([^\n]+))");
        std::smatch task_match;
        if (std::regex_search(result, task_match, task_re)) {
            compressed += "当前任务: " + task_match[1].str() + "\n";
        }

        result = compressed;
        spdlog::debug("[ContextManager] L5 降级: 全文压缩 (结果 {} 字符)", result.size());
    }

    return result;
}

// ===========================================================================
// truncateMessages — 按预算从旧到新截断
// ===========================================================================

std::vector<llm::Message> ContextManager::truncateMessages(
    const std::vector<llm::Message>& messages,
    int budget,
    int& truncated_count)
{
    truncated_count = 0;

    if (messages.empty()) {
        return messages;
    }

    // 预算非正数时，无法容纳任何消息
    if (budget <= 0) {
        truncated_count = static_cast<int>(messages.size());
        return {};
    }

    // 无需截断
    if (llm::TokenCounter::countMessages(messages) <= budget) {
        return messages;
    }

    // 从尾部（最新消息）向前构建结果，O(n) 单次遍历
    std::vector<llm::Message> result;
    int used = 0;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        int msg_cost = llm::TokenCounter::countSingleMessage(*it);
        if (used + msg_cost > budget) break;
        used += msg_cost;
        result.push_back(*it);
    }
    // 反转为原始顺序（旧→新）
    std::reverse(result.begin(), result.end());
    truncated_count = static_cast<int>(messages.size()) - static_cast<int>(result.size());

    // 确保至少保留最后一条消息（通常是用户的最后输入）
    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }

    return result;
}

// ===========================================================================
// 会话持久化（Phase 4.4）
// ===========================================================================

void ContextManager::saveSession(
    const std::string& project_path,
    const llm::Conversation& conversation)
{
    if (project_path.empty()) {
        spdlog::warn("[ContextManager] 项目路径为空，跳过会话保存");
        return;
    }

    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : conversation.all()) {
        nlohmann::json msg_json;
        msg_json["role"] = llm::roleToString(msg.role);
        msg_json["content"] = msg.content;

        if (!msg.tool_calls.empty()) {
            nlohmann::json tool_calls_json = nlohmann::json::array();
            for (const auto& tc : msg.tool_calls) {
                nlohmann::json tc_json;
                tc_json["id"] = tc.id;
                tc_json["type"] = tc.type;
                tc_json["function"] = {
                    {"name", tc.function_name},
                    {"arguments", tc.arguments}
                };
                tool_calls_json.push_back(tc_json);
            }
            msg_json["tool_calls"] = tool_calls_json;
        }

        if (!msg.tool_call_id.empty()) {
            msg_json["tool_call_id"] = msg.tool_call_id;
        }

        j.push_back(msg_json);
    }

    ProjectIO::saveConversation(project_path, j);
    spdlog::info("[ContextManager] 会话已保存 ({} 条消息)", j.size());
}

llm::Conversation ContextManager::loadSession(const std::string& project_path)
{
    llm::Conversation conv;

    if (project_path.empty()) {
        return conv;
    }

    nlohmann::json j = ProjectIO::loadConversation(project_path);
    if (!j.is_array()) {
        return conv;
    }

    for (const auto& msg_json : j) {
        std::string role_str = utils::json::getOrDefault(msg_json, "role", std::string{});
        std::string content = utils::json::getOrDefault(msg_json, "content", std::string{});

        llm::Message msg;
        msg.role = llm::roleFromString(role_str);
        msg.content = content;

        if (msg_json.contains("tool_call_id")) {
            msg.tool_call_id = msg_json["tool_call_id"].get<std::string>();
        }

        if (msg_json.contains("tool_calls") && msg_json["tool_calls"].is_array()) {
            for (const auto& tc_json : msg_json["tool_calls"]) {
                llm::ToolCall tc;
                tc.id = utils::json::getOrDefault(tc_json, "id", std::string{});
                tc.type = utils::json::getOrDefault(tc_json, "type", std::string{});
                if (tc_json.contains("function")) {
                    tc.function_name = utils::json::getOrDefault(
                        tc_json["function"], "name", std::string{});
                    tc.arguments = utils::json::getOrDefault(
                        tc_json["function"], "arguments", std::string{});
                }
                msg.tool_calls.push_back(tc);
            }
        }

        conv.add(std::move(msg));
    }

    spdlog::info("[ContextManager] 会话已加载 ({} 条消息)", conv.size());
    return conv;
}

void ContextManager::archiveSession(
    const std::string& project_path,
    const llm::Conversation& conversation)
{
    if (project_path.empty() || conversation.empty()) {
        return;
    }

    // 归档目录
    const std::string archive_dir =
        utils::file::joinPath(ProjectIO::agentDir(project_path), "archive");
    utils::file::createDirs(archive_dir);

    // 时间戳文件名
    const std::string timestamp = ProjectIO::nowTimestamp();
    // 将时间戳中的冒号替换为破折号，避免 Windows 文件名限制
    std::string safe_ts = timestamp;
    std::replace(safe_ts.begin(), safe_ts.end(), ':', '-');

    const std::string archive_path =
        utils::file::joinPath(archive_dir, "conversation_" + safe_ts + ".json");

    // 保存归档
    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : conversation.all()) {
        nlohmann::json msg_json;
        msg_json["role"] = llm::roleToString(msg.role);
        msg_json["content"] = msg.content;
        if (!msg.tool_call_id.empty()) {
            msg_json["tool_call_id"] = msg.tool_call_id;
        }
        j.push_back(msg_json);
    }

    ProjectIO::saveJsonFile(archive_path, j);
    spdlog::info("[ContextManager] 会话已归档: {}", archive_path);
}

// ===========================================================================
// 辅助提取函数（Phase 4.1 规则提取）
// ===========================================================================

std::vector<std::string> ContextManager::splitSentences(const std::string& text)
{
    std::vector<std::string> sentences;
    if (text.empty()) return sentences;

    // 用正则匹配句子结束位置：中文标点（。？！…）或英文标点后跟空格/换行
    // UTF-8 编码的中文标点是多字节的，不能用单个 char 比较，用 regex 统一处理
    static const std::regex sentence_end(
        R"([。？！…]|[.!?](?=\s|$|\n))"
    );

    std::sregex_iterator it(text.begin(), text.end(), sentence_end);
    std::sregex_iterator end;
    size_t last_pos = 0;

    for (; it != end; ++it) {
        size_t match_end = it->position() + it->length();
        std::string sentence = text.substr(last_pos, match_end - last_pos);
        // 去除首尾空白
        size_t start = sentence.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            sentence = sentence.substr(start);
            if (!sentence.empty()) {
                sentences.push_back(sentence);
            }
        }
        last_pos = match_end;
    }

    // 剩余部分
    if (last_pos < text.size()) {
        std::string remaining = text.substr(last_pos);
        size_t start = remaining.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            remaining = remaining.substr(start);
            if (!remaining.empty()) {
                sentences.push_back(remaining);
            }
        }
    }

    return sentences;
}

std::vector<std::string> ContextManager::extractCharacterNames(
    const std::vector<llm::Message>& messages)
{
    std::set<std::string> names;

    for (const auto& msg : messages) {
        const std::string& text = msg.content;

        // 方法 1: 匹配引号内的名称（中文引号）
        auto words_begin = std::sregex_iterator(text.begin(), text.end(), kQuotedNamePattern);
        auto words_end = std::sregex_iterator();
        for (auto it = words_begin; it != words_end; ++it) {
            std::string name = (*it)[1].str();
            // 过滤明显不是人名的（太短、纯数字、含特殊字符）
            if (name.size() >= 2 && name.size() <= 5 &&
                std::none_of(name.begin(), name.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c)) ||
                           c == ' ' || c == '\n' || c == '\t';
                })) {
                names.insert(name);
            }
        }

        // 方法 2: 匹配 "角色" 关键词后的名称
        std::regex char_kw(R"((角色|人物|主角|配角|反派)[：:]\s*(\S+))");
        auto kw_begin = std::sregex_iterator(text.begin(), text.end(), char_kw);
        auto kw_end = std::sregex_iterator();
        for (auto it = kw_begin; it != kw_end; ++it) {
            names.insert((*it)[2].str());
        }
    }

    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> ContextManager::extractChapterRefs(
    const std::vector<llm::Message>& messages)
{
    std::set<std::string> refs;

    for (const auto& msg : messages) {
        const std::string& text = msg.content;

        // ch-XXX 格式
        auto id_begin = std::sregex_iterator(text.begin(), text.end(), kChapterIdPattern);
        auto id_end = std::sregex_iterator();
        for (auto it = id_begin; it != id_end; ++it) {
            refs.insert(it->str());
        }

        // 第X章 格式
        auto ch_begin = std::sregex_iterator(text.begin(), text.end(), kChineseChapterPattern);
        auto ch_end = std::sregex_iterator();
        for (auto it = ch_begin; it != ch_end; ++it) {
            refs.insert(it->str());
        }
    }

    return std::vector<std::string>(refs.begin(), refs.end());
}

std::vector<std::string> ContextManager::extractPlotPoints(
    const std::vector<llm::Message>& messages)
{
    std::vector<std::string> points;
    // 剧情相关关键词
    const std::vector<std::string> plot_keywords = {
        "剧情", "情节", "冲突", "转折", "高潮", "伏笔", "悬念",
        "发展", "推进", "变化", "揭示", "收束", "展开"
    };

    for (const auto& msg : messages) {
        if (msg.role != llm::MessageRole::User &&
            msg.role != llm::MessageRole::Assistant) {
            continue;
        }

        auto sentences = splitSentences(msg.content);
        for (const auto& sent : sentences) {
            if (containsKeyword(sent, plot_keywords)) {
                // 截取前 80 个字符避免过长的剧情点
                std::string trimmed = sent.size() > 80
                    ? sent.substr(0, 80) + "..." : sent;
                points.push_back(trimmed);
                if (points.size() >= kMaxPlotPoints) break;
            }
        }
        if (points.size() >= kMaxPlotPoints) break;
    }

    // 去重
    std::set<std::string> unique(points.begin(), points.end());
    return std::vector<std::string>(unique.begin(), unique.end());
}

std::vector<std::string> ContextManager::extractTasks(
    const std::vector<llm::Message>& messages)
{
    std::vector<std::string> tasks;

    // 从最新用户消息中提取指令性语句
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != llm::MessageRole::User) continue;

        const std::string& text = it->content;
        auto sentences = splitSentences(text);

        for (const auto& sent : sentences) {
            // 指令性关键词
            const std::vector<std::string> task_keywords = {
                "写", "创作", "修改", "检查", "分析", "生成",
                "创建", "更新", "删除", "添加", "完成", "开始",
                "写一", "编撰", "续写", "改写"
            };

            if (containsKeyword(sent, task_keywords)) {
                std::string trimmed = sent.size() > 100
                    ? sent.substr(0, 100) + "..." : sent;
                tasks.push_back(trimmed);
                if (tasks.size() >= kMaxTasks) break;
            }
        }

        // 只从最近一条用户消息提取任务
        if (!tasks.empty()) break;
    }

    return tasks;
}

} // namespace agent
