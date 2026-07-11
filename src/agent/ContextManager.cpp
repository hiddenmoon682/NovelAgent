// ContextManager 实现 — 会话追踪 + compaction + 持久化。
// Compactor 类已展开合并到本文件，消除冗余常量和转发方法。

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
// 取项目设定文件的最后修改时间戳（取多个文件中的最新值）。
int64_t projectSettingsMtime(const std::string& project_path) {
    if (project_path.empty()) return 0;

    static const std::array<const char*, 5> kSettingFiles = {
        ProjectIO::kNovelJsonFileName,
        ProjectIO::kOutlineJsonFileName,
        ProjectIO::kCharactersJsonFileName,
        ProjectIO::kSettingsJsonFileName,
        ProjectIO::kWorldRulesJsonFileName
    };

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

// 默认存储占位符（不使用持久化功能时的回退）。
FileStorageBackend& defaultStorage() {
    static FileStorageBackend s("");
    return s;
}

// ── Compaction 常量 ──
// Compaction 时的 system prompt — 双层摘要：情节事实 + 风格样本。
constexpr const char* kCompactSystemPrompt =
    "你是一个小说创作助手的上下文压缩器。用中文对以下对话历史进行双层摘要：\n"
    "\n"
    "1. 情节事实：角色决策与性格变化、情节转折与关键事件、\n"
    "   世界观设定变更、未解决的伏笔与冲突、待完成任务与下一步计划\n"
    "\n"
    "2. 风格参考：摘录 2-3 句最能代表当前写作风格的原句——\n"
    "   保留其修辞手法、句式节奏、情绪氛围和对话语气\n"
    "\n"
    "总长度控制在 2000 字以内，事实与风格的比例由你判断。\n"
    "\n"
    "注意：对话历史中可能包含之前生成的压缩摘要，\n"
    "请以已有摘要中的情节事实为基础，补充新增对话中的关键进展，\n"
    "避免过度概括或丢失已有摘要中的细节。";

// Compaction 时保留的最近消息对数。
constexpr int kCompactKeepExchanges = 10;   // 保留最近 10 对 = ~20 条消息
constexpr int kMinKeepExchanges = 2;        // 最少保留 2 对 = 4 条消息

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
    meta.compacted_summary = summary_;
    meta.compaction_marker = marker_;
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

    // 恢复到 TokenTracker + Compaction 状态
    restoreCompactionState(meta.compacted_summary, meta.compaction_marker);
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
    summary_.clear();
    marker_ = 0;
    last_warnings_.clear();
}

// ===========================================================================
// Compaction
// ===========================================================================

CompactResult ContextManager::compact(
    llm::Conversation& conversation,
    llm::ILLMClient& llm_client,
    std::optional<std::string> focus)
{
    CompactResult result;
    const auto& all_msgs = conversation.all();
    int total_msgs = static_cast<int>(all_msgs.size());

    // 计算保留边界：理想保留 kCompactKeepExchanges*2 条。
    const int ideal_keep = kCompactKeepExchanges * 2;   // 20
    const int min_keep = kMinKeepExchanges * 2;          // 4

    if (total_msgs <= 1) {
        spdlog::info("[ContextManager] compact 跳过: 没有可压缩的消息 ({} 条)", total_msgs);
        result.summary = "(消息数量不足，无法压缩)";
        return result;
    }

    int keep_count = ideal_keep;
    if (total_msgs <= ideal_keep) {
        keep_count = std::min(min_keep, total_msgs - 1);
        if (keep_count == 0) keep_count = 1;
    }
    int compact_count = total_msgs - keep_count;
    result.messages_compacted = compact_count;

    // 估算压缩前后的 token 数
    std::vector<llm::Message> to_compact(all_msgs.begin(), all_msgs.begin() + compact_count);
    result.tokens_before = llm::TokenCounter::countMessages(to_compact);

    // 拼接待压缩消息为文本
    std::ostringstream oss;
    for (const auto& msg : to_compact) {
        std::string role_str;
        switch (msg.role) {
            case llm::MessageRole::User:       role_str = "用户"; break;
            case llm::MessageRole::Assistant:  role_str = "助手"; break;
            case llm::MessageRole::Tool:       role_str = "工具"; break;
            default: continue;
        }
        oss << "[" << role_str << "] " << msg.content << "\n";
    }
    std::string conversation_text = oss.str();

    // 构建压缩提示词
    std::string compact_prompt = buildCompactPrompt(conversation_text, focus);

    std::vector<llm::Message> compact_msgs = {
        llm::Message::user(conversation_text)
    };

    try {
        // 计算发送的估算 token（供校准回传使用）
        int send_convo = estimateTokens(conversation_text);
        int send_prompt = estimateTokens(compact_prompt);
        int estimated_input = send_convo + send_prompt + 50;

        auto response = llm_client.chatNonStreaming(compact_msgs, {}, compact_prompt);
        result.summary = response.content;
        result.tokens_after = response.completion_tokens;

        // 将 compaction LLM 调用的真实 token 数回传给校准器
        if (calibrator_ && !model_name_.empty() && estimated_input > 0 && response.prompt_tokens > 0) {
            calibrator_->calibrate(model_name_, estimated_input, response.prompt_tokens);
            spdlog::debug("[ContextManager] Token 校准: model={}, estimated={}, actual={}",
                          model_name_, estimated_input, response.prompt_tokens);
        }

        summary_ = result.summary;
        marker_ = compact_count;

        // 从 conversation 头部删除已压缩的旧消息
        conversation.removeOldest(compact_count);

        // 将被压缩的对话摘要以 user/assistant 消息对插入对话头部
        conversation.addUser("【系统】以下是被压缩的旧对话摘要：");
        conversation.addAssistant("[被压缩的历史摘要]\n" + result.summary);

        spdlog::info("[ContextManager] compact 完成: {} 条 → 摘要 ({} → {} tokens, {:.0f}%)",
                     compact_count, result.tokens_before, result.tokens_after,
                     result.tokens_before > 0
                         ? (1.0 - static_cast<double>(result.tokens_after) / result.tokens_before) * 100.0
                         : 0.0);
    } catch (const std::exception& e) {
        spdlog::error("[ContextManager] compact LLM 调用失败: {}", e.what());
        result.summary = "(压缩失败: " + std::string(e.what()) + ")";
        result.messages_compacted = 0;
    }

    return result;
}

void ContextManager::setAutoCompact(bool enabled, int threshold_pct) {
    auto_compact_ = enabled;
    if (threshold_pct > 0 && threshold_pct <= 100)
        auto_compact_threshold_ = threshold_pct;
}

bool ContextManager::shouldAutoCompact(int usage_percent) const {
    if (!auto_compact_) return false;
    return usage_percent >= auto_compact_threshold_;
}

// ===========================================================================
// assemble — 上下文组装核心方法
//
// 执行流程（共 5 步）：
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 0: System Prompt 构建                                  │
//   │  buildSystemPrompt(project)                                 │
//   │  → 输出项目级静态上下文（标题、Logline、主题、工具指南）    │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 1: Token 预算                                          │
//   │  sys_tokens  = countTokens(system_prompt)                   │
//   │  msg_budget = max(0, max_context_tokens - sys_tokens)      │
//   │  ● msg_budget <= 0 → error：system prompt 独占窗口          │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 2: 实时用量计算 + 自动压缩检查                          │
//   │  total_tokens = sys_tokens + msg_tokens（含新用户输入）      │
//   │  ● 自动压缩（llm_client 非空 + 启用 + 超阈值）：              │
//   │    compact() → 压缩成功则重算 total_tokens                   │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 3: 告警                                                │
//   │  total_tokens / model_limit → 三级预警（实时数据）：          │
//   │  ● Normal  ( <  60% ) → 静默                                │
//   │  ● Warning (60%-85%) → 建议 /compact                       │
//   │  ● Critical( ≥ 85% ) → 强烈建议 /compact                    │
//   │  ●  > 100% → 致命告警                                       │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 4: 内部状态缓存                                        │
//   │  last_warnings_     → 供 Agent/REPL 读取                    │
//   │  current_context_size_ → 更新为 total_tokens                │
//   └─────────────────────────────────────────────────────────────┘
// ===========================================================================

ContextAssembly ContextManager::assemble(
    llm::Conversation& conversation,
    int max_context_tokens,
    llm::ILLMClient* llm_client)
{
    ContextAssembly result;

    // ── 步骤 0: System Prompt 构建 ──────────────────────────────────────
    if (project_) {
        result.system_prompt = buildSystemPrompt(*project_);
    }

    // ── 步骤 1: Token 预算 ──────────────────────────────────────────────
    int sys_tokens = llm::TokenCounter::countTokensCalibrated(result.system_prompt, model_name_, calibrator_);
    int msg_budget = std::max(0, max_context_tokens - sys_tokens);

    if (msg_budget <= 0) {
        result.warnings.push_back(
            "System prompt 已占用全部预算（" + std::to_string(sys_tokens) +
            " tokens），无法容纳对话历史。建议精简项目上下文或增加 max_context_tokens。");
        spdlog::error("[ContextManager] msg_budget <= 0 (sys={}, max={})",
                      sys_tokens, max_context_tokens);
    }

    // ── 步骤 2: 实时用量计算 + 自动压缩检查 ────────────────────────────
    result.messages = conversation.messages();
    int msg_tokens = llm::TokenCounter::countMessagesCalibrated(result.messages, model_name_, calibrator_);
    result.total_tokens = sys_tokens + msg_tokens;

    // 基于本轮实时 total_tokens 判断是否需要自动压缩。
    // 直接计算 usage_pct，不依赖 tracker_.usagePercent()（那是上一轮的陈旧数据）。
    int model_limit = tracker_.modelLimit();
    if (llm_client && auto_compact_ && model_limit > 0) {
        int usage_pct = (result.total_tokens * 100) / model_limit;
        if (usage_pct >= auto_compact_threshold_) {
            spdlog::info("[ContextManager] 自动压缩触发 (用量 {}%, 阈值 {}%)",
                         usage_pct, auto_compact_threshold_);
            auto cr = compact(conversation, *llm_client, "自动压缩：上下文用量过高");
            if (cr.messages_compacted > 0) {
                // 压缩成功 — 重新计算（conversation 已被 compact() 修改）
                result.messages = conversation.messages();
                msg_tokens = llm::TokenCounter::countMessagesCalibrated(result.messages, model_name_, calibrator_);
                result.total_tokens = sys_tokens + msg_tokens;
                spdlog::info("[ContextManager] 自动压缩完成: {} 条 → 摘要, 新用量 {} tokens",
                             cr.messages_compacted, result.total_tokens);
            }
        }
    }

    // ── 步骤 3: 实时用量告警 ────────────────────────────────────────────
    auto pre_check = checkThresholds(result.total_tokens);
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

    // ── 步骤 4: 缓存到内部状态 ─────────────────────────────────────────
    last_warnings_ = result.warnings;
    tracker_.setCurrentContextSize(result.total_tokens);

    return result;
}

// ===========================================================================
// buildSystemPrompt — 构建项目级静态上下文
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project)
{
    std::string prompt;
    prompt += "# 项目: " + project.title + "\n";
    if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
    if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";

    prompt += "\n" + prompt::PromptContextBuilder::renderToolUseInstructions();

    return prompt;
}

// ===========================================================================
// 私有辅助方法
// ===========================================================================

std::string ContextManager::buildCompactPrompt(
    const std::string& conversation_text,
    const std::optional<std::string>& focus) const
{
    std::string compact_prompt = kCompactSystemPrompt;

    // 检查是否超出模型窗口，超出则截断对话文本
    int convo_tokens = estimateTokens(conversation_text);
    int prompt_tokens = estimateTokens(compact_prompt);
    int total_estimated = convo_tokens + prompt_tokens + 50;
    int model_limit = tracker_.modelLimit();

    std::string text = conversation_text;  // 可修改的副本

    if (model_limit > 0 && total_estimated > model_limit) {
        spdlog::warn("[ContextManager] 待压缩对话 {} tokens 超过模型窗口 {}，"
                     "截断后再提交", total_estimated, model_limit);
        size_t max_convo_chars = text.size()
            * model_limit / total_estimated * 7 / 10;
        if (max_convo_chars < text.size()) {
            max_convo_chars = (std::max)(max_convo_chars, size_t{500});
            text = text.substr(0, max_convo_chars) + "\n...(已截断)";
        }
    }

    if (focus && !focus->empty()) {
        compact_prompt += std::string("\n特别注意：") + *focus;
    }

    return compact_prompt;
}

int ContextManager::estimateTokens(const std::string& text) const {
    return llm::TokenCounter::countTokensCalibrated(text, model_name_, calibrator_);
}

} // namespace agent
