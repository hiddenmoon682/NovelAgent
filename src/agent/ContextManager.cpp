// ContextManager 实现 — 增强版（会话追踪 + pin + compaction + 降级可见性）。

#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "agent/PromptContextBuilder.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <sstream>

namespace agent {

namespace {
// 默认存储占位符（不使用持久化功能时的回退）。
FileStorageBackend& defaultStorage() {
    static FileStorageBackend s("");
    return s;
}

// 取项目设定文件的最后修改时间戳（取多个文件中的最新值）。
// 用于会话恢复时检测"项目在保存后被外部修改"→ 清空旧摘要。
// 返回 0 表示取不到（项目路径为空或所有文件均不存在）。
//
// A12 修复：此前只盯 project.json（后改为 novel.json，A5 已修），颗粒度不足——
//   修改角色/设定/规则时，characters.json/settings.json/world_rules.json 等文件才会变，
//   novel.json 可能不变。现改为取 novel.json + outline.json + characters.json +
//   settings.json + world_rules.json 的最新 mtime，覆盖全部主要 JSON 设定文件。
//   局限：章节正文（.md 文件）不在检测范围内（A12 根治需工具层标记向量失效）。
int64_t projectSettingsMtime(const std::string& project_path) {
    if (project_path.empty()) return 0;

    // 取多个设定 JSON 文件的最新 mtime，覆盖角色/大纲/设定/规则变更
    static const std::array<const char*, 5> kSettingFiles = {
        ProjectIO::kNovelJsonFileName,           // novel.json — 项目元数据
        ProjectIO::kOutlineJsonFileName,          // outline.json — 大纲
        ProjectIO::kCharactersJsonFileName,       // characters.json — 角色
        ProjectIO::kSettingsJsonFileName,         // settings.json — 设定
        ProjectIO::kWorldRulesJsonFileName        // world_rules.json — 世界规则
    };

    // MinGW 实现下 last_write_time().time_since_epoch().count() 可能为负值或零，
    // 但跨文件比较时方向一致。此处取所有存在文件的最新值。
    int64_t latest = std::numeric_limits<int64_t>::min();
    bool any_found = false;
    for (const auto* fname : kSettingFiles) {
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(project_path + "/" + fname, ec);
        if (!ec) {
            any_found = true;
            int64_t t = ftime.time_since_epoch().count();
            if (t > latest) latest = t;
        }
    }
    return any_found ? latest : 0;
}

// Compaction 时保留的最近消息对数。
constexpr int kCompactKeepExchanges = 10;   // 保留最近 10 对 = ~20 条消息

// Compaction 用的 system prompt — 双层摘要：情节事实 + 风格样本。
constexpr const char* kCompactSystemPrompt =
    "你是一个小说创作助手的上下文压缩器。用中文对以下对话历史进行双层摘要：\n"
    "\n"
    "1. 情节事实：角色决策与性格变化、情节转折与关键事件、\n"
    "   世界观设定变更、未解决的伏笔与冲突、待完成任务与下一步计划\n"
    "\n"
    "2. 风格参考：摘录 2-3 句最能代表当前写作风格的原句——\n"
    "   保留其修辞手法、句式节奏、情绪氛围和对话语气\n"
    "\n"
    "总长度控制在 2000 字以内，事实与风格的比例由你判断。";

// Token 用量告警阈值。
constexpr int kWarnPercent = 60;
constexpr int kCriticalPercent = 85;
} // namespace

// ===========================================================================
// 构造
// ===========================================================================

ContextManager::ContextManager()
    : storage_(defaultStorage())
    , persistence_(storage_)
{}

ContextManager::ContextManager(FileStorageBackend& storage)
    : storage_(storage)
    , persistence_(storage)
{}

// ===========================================================================
// 会话状态持久化
// ===========================================================================

void ContextManager::saveSessionState(
    const llm::Conversation& conv,
    const std::vector<size_t>& preserved_indices)
{
    persistence_.save(conv);

    const int64_t mtime = project_ ? projectSettingsMtime(project_->path) : 0;

    SessionMeta meta;
    meta.compacted_summary = compactor_.summary();
    meta.compaction_marker = compactor_.marker();
    meta.token_state = tracker_.snapshot();
    meta.preserved_indices = preserved_indices;
    meta.project_mtime = mtime;
    persistence_.saveMeta(meta);

    spdlog::info("[ContextManager] 完整会话状态已保存 (mtime={})", mtime);
}

void ContextManager::loadSessionState(
    llm::Conversation& conv)
{
    conv = persistence_.load();
    auto meta = persistence_.loadMeta();

    const int64_t current_mtime = project_ ? projectSettingsMtime(project_->path) : 0;

    if (current_mtime > 0 && meta.project_mtime > 0
        && current_mtime != meta.project_mtime) {
        spdlog::warn("[ContextManager] Project 设定已变更 (mtime {} → {})，清空旧摘要",
                     meta.project_mtime, current_mtime);
        meta.compacted_summary.clear();
        meta.compaction_marker = 0;
    }

    // 恢复到 Compactor + TokenTracker 状态
    compactor_.restore(meta.compacted_summary, meta.compaction_marker);
    tracker_.restore(meta.token_state);

    // 恢复 preserved 标记
    for (auto idx : meta.preserved_indices) {
        conv.pinMessage(idx);
    }

    spdlog::info("[ContextManager] 完整会话状态已恢复 (消息={}, preserved={}, compact={}, requests={})",
                 conv.size(), meta.preserved_indices.size(),
                 !meta.compacted_summary.empty(), meta.token_state.request_count);
}

void ContextManager::resetSession() {
    tracker_.reset();
    compactor_.clear();
    last_warnings_.clear();
    last_truncated_count_ = 0;
}

// ===========================================================================
// Compaction 委托
// ===========================================================================

CompactResult ContextManager::compact(
    llm::Conversation& conversation,
    llm::ILLMClient& llm_client,
    std::optional<std::string> focus)
{
    auto result = compactor_.compact(conversation, llm_client,
                                     std::move(focus));
    return result;
}

void ContextManager::setAutoCompact(bool enabled, int threshold_pct) {
    compactor_.setAutoCompact(enabled, threshold_pct);
}

bool ContextManager::shouldAutoCompact() const {
    return compactor_.shouldAutoCompact(tracker_.usagePercent());
}

// ===========================================================================
// assemble — 上下文组装核心方法
//
// 职责：
//   在每次 LLM 请求前，将静态项目设定与当前对话历史合并，
//   在 max_context_tokens 预算内执行消息截断，
//   并输出 token 用量告警，最终产出完整的 ContextAssembly。
//
// 执行流程（共 6 步）：
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 0: System Prompt 构建                                  │
//   │  buildSystemPrompt(project)                                 │
//   │  → 输出项目级静态上下文（标题、Logline、主题、工具指南）    │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 1: Token 预算分配                                      │
//   │  sys_tokens  = countTokens(system_prompt)                   │
//   │  msg_budget = max(0, max_context_tokens - sys_tokens)      │
//   │  → 剩余预算全部留给对话消息列表                              │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 2: Token 阈值告警                                      │
//   │  checkThresholds() → 分三级状态：                            │
//   │  ● Normal  ( < 60% )  → 静默                                │
//   │  ● Warning (60%-85%)  → 建议 /compact                       │
//   │  ● Critical( ≥ 85% )  → 强烈建议 /compact                    │
//   │  ● msg_budget <= 0    → error：system prompt 独占窗口       │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 3: 对话消息截断                                        │
//   │  truncateMessages(all_msgs, msg_budget, truncated_count)    │
//   │  → 从最新消息反向贪心保留，preserved 消息优先但不免预算       │
//   │  → 安全兜底：至少保留最后一条消息（当前用户输入）             │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 4: 最终 Token 统计                                     │
//   │  total_tokens = sys_tokens + countMessages(result.messages) │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 5: 模型窗口超限预检（API 400 防护）                    │
//   │  total_tokens > model_context_limit → 致命告警               │
//   │  提示用户减少 /pin 数量或执行 /compact                       │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 6: 内部状态缓存                                        │
//   │  last_warnings_        → 供 Agent/REPL 读取                  │
//   │  last_truncated_count_ → 供外部查询截断数                    │
//   │  current_context_size_ → 更新为 total_tokens，供下次阈值检查 │
//   └─────────────────────────────────────────────────────────────┘
//
// 输出 ContextAssembly 关键字段：
//   system_prompt         — 项目上下文的拼接结果
//   messages              — 截断后的有效消息列表
//   warnings              — 所有告警的文本数组
//   total_tokens          — 最终发送的 token 总量
//   truncated_count       — 被丢弃的消息数量
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int max_context_tokens)
{
    ContextAssembly result;

    // ── 步骤 0: System Prompt 构建 ──────────────────────────────────────
    // 输出项目概要 + 工具使用指南，LLM 通过工具按需获取章节/角色/设定等上下文。
    if (project_) {
        result.system_prompt = buildSystemPrompt(*project_);
    }

    // ── 步骤 1: Token 预算分配 ────────────────────────────────────────────
    // 总预算 = max_context_tokens；system_prompt 优先占用，剩余归消息列表。
    int sys_tokens = result.system_prompt.empty()
        ? 0 : llm::TokenCounter::countTokens(result.system_prompt);
    // 应用 Token 校准修正因子
    sys_tokens = calibrateToken(sys_tokens);
    int msg_budget = std::max(0, max_context_tokens - sys_tokens);

    // ── 步骤 2: 生成 Token 用量告警 ──────────────────────────────────────
    // 基于最后一次请求的实际上下文大小（非累计值）分三级预警。
    auto pre_check = checkThresholds();
    if (pre_check.status == ContextStatus::Critical) {
        result.warnings.push_back(
            "上下文用量已达 " + std::to_string(pre_check.usage_percent) +
            "%，接近模型上限。建议使用 /compact 压缩对话历史。");
        spdlog::warn("[ContextManager] 上下文用量临界: {}%", pre_check.usage_percent);
    } else if (pre_check.status == ContextStatus::Warning) {
        result.warnings.push_back(
            "上下文用量 " + std::to_string(pre_check.usage_percent) +
            "%，可考虑 /compact 释放空间。");
    }

    // msg_budget <= 0 意味着静态上下文已占满窗口 → 消息列表无任何预算
    if (msg_budget <= 0) {
        result.warnings.push_back(
            "System prompt 已占用全部预算（" + std::to_string(sys_tokens) +
            " tokens），无法容纳对话历史。建议精简项目上下文或增加 max_context_tokens。");
        spdlog::error("[ContextManager] msg_budget <= 0 (sys={}, max={})",
                      sys_tokens, max_context_tokens);
    }

    // ── 步骤 3: 截断消息（支持 preserved 标记）───────────────────────────
    // truncateMessages 内部按 "preserved 优先 → 从最新反向贪心" 的策略
    // 在 msg_budget 内尽可能保留更多消息。
    const auto& all_msgs = conversation.messages();
    result.messages = truncateMessages(all_msgs, msg_budget, result.truncated_count);

    if (result.truncated_count > 0) {
        result.warnings.push_back(
            "对话历史已截断 " + std::to_string(result.truncated_count) +
            " 条消息，旧内容可能丢失。使用 /compact 可压缩保留关键信息。");
        spdlog::warn("[ContextManager] 截断 {} 条消息 (预算={} sys={} msg_budget={})",
                     result.truncated_count, max_context_tokens, sys_tokens, msg_budget);
    }

    // ── 步骤 4: 统计总 token ────────────────────────────────────────────
    int msg_tokens = llm::TokenCounter::countMessages(result.messages);
    // 应用 Token 校准修正因子
    msg_tokens = calibrateToken(msg_tokens);
    result.total_tokens = sys_tokens + msg_tokens;

    // ── 步骤 5: 最终预检 ──────────────────────────────────────────────
    // 在即将发送前再检查一次：总 token 是否超出模型上下文窗口上限。
    // 这是防止 LLM API 返回 400 Bad Request 的最后一道防线。
    int model_limit = tracker_.modelLimit();
    if (model_limit > 0
        && result.total_tokens > model_limit) {
        result.warnings.push_back(
            "⚠ 总 token(" + std::to_string(result.total_tokens)
            + ") 超出模型窗口(" + std::to_string(model_limit)
            + ")，请求可能被 API 拒绝。请减少 /pin 数量或 /compact 压缩。");
        spdlog::error("[ContextManager] 总 token({}) 超出模型窗口({})",
                      result.total_tokens, model_limit);
    }

    // ── 步骤 6: 缓存到内部状态 ──────────────────────────────────────────
    // 供 Agent / REPL 层查询：last_warnings_ 用于展示给用户，
    // last_truncated_count_ 用于统计，current_context_size_ 用于下次阈值检查。
    last_warnings_ = result.warnings;
    last_truncated_count_ = result.truncated_count;
    tracker_.setCurrentContextSize(result.total_tokens);

    return result;
}

// ===========================================================================
// buildSystemPrompt — 构建项目级静态上下文
//
// 职责：
//   为 LLM 请求提供静态项目信息（标题、Logline、主题），
//   并附加按需获取上下文的工具使用指南。
//   LLM 通过 get_latest_chapter / get_chapter_context / get_relevant_characters
//   等工具按需获取章节、角色、设定等详情，不再自动注入章节级上下文。
//
// 设计：始终输出项目级概要，不注入章节特定信息。
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project)
{
    std::string prompt;
    prompt += "# 项目: " + project.title + "\n";
    if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
    if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";

    // 附加按需获取上下文的工具使用指南
    prompt += "\n" + prompt::PromptContextBuilder::renderToolUseInstructions();

    return prompt;
}
// ===========================================================================
// truncateMessages — 支持 preserved 标记（纯函数，Issue 3 候选迁移至 utils）
// ===========================================================================

std::vector<llm::Message> ContextManager::truncateMessages(
    const std::vector<llm::Message>& messages,
    int budget,
    int& truncated_count)
{
    truncated_count = 0;
    if (messages.empty()) return messages;

    std::vector<llm::Message> preserved_msgs;
    std::vector<const llm::Message*> normal_msgs;
    for (const auto& msg : messages) {
        if (msg.preserved) preserved_msgs.push_back(msg);
        else normal_msgs.push_back(&msg);
    }

    int preserved_tokens = llm::TokenCounter::countMessages(preserved_msgs);
    // 应用 Token 校准修正因子（preserved 消息的 token 估算可能被高估/低估）
    preserved_tokens = calibrateToken(preserved_tokens);
    int remaining_budget = budget - preserved_tokens;

    if (remaining_budget < 0) {
        spdlog::warn("[ContextManager] preserved 消息已占 {} tokens，超出预算 {} tokens",
                     preserved_tokens, -remaining_budget);
        truncated_count = static_cast<int>(normal_msgs.size());
        if (!normal_msgs.empty()) {
            preserved_msgs.push_back(*normal_msgs.back());
            --truncated_count;
        }
        return preserved_msgs;
    }

    // 快速路径：全部消息都在预算内则无需截断（估算已校准）
    if (calibrateToken(llm::TokenCounter::countMessages(messages)) <= budget) return messages;

    std::vector<llm::Message> result;
    int used = 0;
    for (auto it = normal_msgs.rbegin(); it != normal_msgs.rend(); ++it) {
        int cost = calibrateToken(llm::TokenCounter::countSingleMessage(**it));
        if (used + cost > remaining_budget) break;
        used += cost;
        result.push_back(**it);
    }
    std::reverse(result.begin(), result.end());

    for (auto it = preserved_msgs.rbegin(); it != preserved_msgs.rend(); ++it)
        result.insert(result.begin(), *it);

    truncated_count = static_cast<int>(messages.size()) - static_cast<int>(result.size());

    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }
    return result;
}

} // namespace agent
