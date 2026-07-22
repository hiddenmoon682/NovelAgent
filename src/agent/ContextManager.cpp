// ContextManager 实现 — system prompt 构建 + 对话压缩 + 自动压缩 + 持久化。
//
// Token 追踪委托给 TokenTracker，会话持久化委托给 SessionPersistence。
// Compaction 逻辑已内聚到本文件（不再通过独立的 Compactor 类转发）。

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

// ── 压缩常量 ──
// 压缩时的 system prompt — 双层摘要：情节事实 + 风格样本。
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
constexpr int kCompactKeepExchanges = 5;   // 保留最近 5 对 = 10 条消息
constexpr int kMinKeepExchanges = 2;        // 最少保留 2 对 = 4 条消息
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
    const llm::IMemory& mem,
    const std::vector<size_t>& preserved_indices)
{
    persistence_.save(mem);

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
    llm::IMemory& mem)
{
    auto loaded = persistence_.load();
    mem.restore(loaded.checkpoint());
    auto meta = persistence_.loadMeta();

    const int64_t current_mtime = project_ ? projectSettingsMtime(project_->path) : 0;

    if (current_mtime > 0 && meta.project_mtime > 0
        && current_mtime != meta.project_mtime) {
        spdlog::warn("[ContextManager] Project 设定已变更 (mtime {} → {})，清空旧摘要",
                     meta.project_mtime, current_mtime);
        meta.compacted_summary.clear();
        meta.compaction_marker = 0;
    }

    // 恢复到 TokenTracker + 压缩状态
    restoreCompactionState(meta.compacted_summary, meta.compaction_marker);
    tracker_.restore(meta.token_state);

    // 恢复 preserved 标记
    for (auto idx : meta.preserved_indices) {
        mem.pin(idx);
    }

    spdlog::info("[ContextManager] 完整会话状态已恢复 (消息={}, preserved={}, compact={}, requests={})",
                 mem.size(), meta.preserved_indices.size(),
                 !meta.compacted_summary.empty(), meta.token_state.request_count);
}

void ContextManager::resetSession() {
    tracker_.reset();
    summary_.clear();
    marker_ = 0;
    last_warnings_.clear();
}

// ===========================================================================
// 压缩
// ===========================================================================

CompactResult ContextManager::compact(
    llm::IMemory& memory,
    llm::ILLMClient& llm_client,
    std::optional<std::string> focus)
{
    CompactResult result;
    const auto& all_msgs = memory.messages();
    int total_msgs = static_cast<int>(all_msgs.size());

    // 保留最近 kCompactKeepExchanges 对消息，压缩更早的历史。
    // 消息不足时按比例缩减保留数，最少保留 1 条。
    const int ideal_keep = kCompactKeepExchanges * 2;   // 10
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
    result.tokens_before = llm::TokenCounter::countMessagesCalibrated(to_compact, model_name_, calibrator_);

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
        if (msg.role == llm::MessageRole::Assistant && !msg.reasoning_content.empty()) {
            oss << "  [思考过程] " << msg.reasoning_content << "\n";
        }
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

        // 将 compact LLM 调用的 token 消耗计入会话统计
        // （保存并恢复 current_context_size_，防止覆盖主对话的用量快照）
        int saved_ctx = tracker_.currentContextSize();
        tracker_.record(response.prompt_tokens, response.completion_tokens);
        tracker_.setCurrentContextSize(saved_ctx);

        // 将 compaction LLM 调用的真实 token 数回传给校准器
        if (calibrator_ && !model_name_.empty() && estimated_input > 0 && response.prompt_tokens > 0) {
            calibrator_->calibrate(model_name_, estimated_input, response.prompt_tokens);
            spdlog::debug("[ContextManager] Token 校准: model={}, estimated={}, actual={}",
                          model_name_, estimated_input, response.prompt_tokens);
        }

        // 从 memory 头部删除已压缩的旧消息
        memory.removeOldest(compact_count);

        // 将被压缩的对话摘要以 user/assistant 消息对插入头部，
        // 替代刚被删除的旧消息的时序位置（必须在保留的最近消息之前）。
        // 注意先 prepend assistant 再 prepend user，最终顺序为 user→assistant。
        memory.prepend(llm::Message::assistant("[被压缩的历史摘要]\n" + result.summary));
        memory.prepend(llm::Message::user("【系统】以下是被压缩的旧对话摘要："));

        // 在对话修改成功后统一更新内部状态，避免异常路径下 summary_/marker_ 不一致
        summary_ = result.summary;
        marker_ = compact_count;

        // 更新上下文用量快照为压缩后的新对话大小
        int new_ctx = llm::TokenCounter::countMessagesCalibrated(
            memory.messages(), model_name_, calibrator_);
        tracker_.setCurrentContextSize(new_ctx);

        spdlog::info("[ContextManager] compact 完成: {} 条 → 摘要 ({} → {} tokens, {:.0f}%)",
                     compact_count, result.tokens_before, result.tokens_after,
                     result.tokens_before > 0
                         ? (1.0 - static_cast<double>(result.tokens_after) / result.tokens_before) * 100.0
                         : 0.0);
    } catch (const std::exception& e) {
        spdlog::error("[ContextManager] compact LLM 调用失败: {}", e.what());
        result.summary = "(压缩失败: " + std::string(e.what()) + ")";
        result.messages_compacted = 0;
        // 不清空 summary_/marker_：对话未被修改（removeOldest 和 prepend 在成功分支），
        // 上次成功的摘要仍然有效且与对话状态一致。
    }

    return result;
}

void ContextManager::setAutoCompact(bool enabled, int threshold_pct) {
    auto_compact_ = enabled;
    if (threshold_pct > 0 && threshold_pct <= 100)
        tracker_.setAutoCompactThreshold(threshold_pct);
}

bool ContextManager::shouldAutoCompact(int usage_percent) const {
    if (!auto_compact_) return false;
    return usage_percent >= tracker_.autoCompactThreshold();
}

// ===========================================================================
// assemble — 上下文组装核心方法
//
// 执行流程（共 4 步）：
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 1: System Prompt 构建                                  │
//   │  buildSystemPrompt(project)                                 │
//   │  → 输出项目级静态上下文（标题、Logline、主题、工具指南）    │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 2: Token 预算                                          │
//   │  sys_tokens  = countTokens(system_prompt)                   │
//   │  msg_budget = max(0, max_context_tokens - sys_tokens)      │
//   │  ● msg_budget <= 0 → error：system prompt 独占窗口          │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 3: 实时用量检查 + 自动压缩 + 告警（三级决策）           │
//   │  checkThresholds(total_tokens) → Normal/Warning/Critical    │
//   │                                                             │
//   │  ● 用量 ≥ auto_compact_threshold_ + 条件满足 → compact()   │
//   │    压缩成功 → 重算 total_tokens → 重新 checkThresholds      │
//   │                                                             │
//   │  ● Critical ( ≥ 85% )  → 强告警                             │
//   │  ● Warning  (60%-84%)  → 软告警                             │
//   │  ● Normal   ( < 60% )  → 静默                               │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 4: 内部状态缓存                                        │
//   │  last_warnings_     → 供 Agent/REPL 读取                    │
//   │  current_context_size_ → 更新为 total_tokens                │
//   └─────────────────────────────────────────────────────────────┘
// ===========================================================================

ContextAssembly ContextManager::assemble(
    llm::IMemory& memory,
    llm::ILLMClient* llm_client)
{
    ContextAssembly result;

    // ── 步骤 0: System Prompt 构建 ──────────────────────────────────────
    if (project_) {
        result.system_prompt = buildSystemPrompt(*project_);
    }

    // ── 步骤 1: Token 预算 ──────────────────────────────────────────────
    int model_limit = tracker_.modelLimit();
    int sys_tokens = llm::TokenCounter::countTokensCalibrated(result.system_prompt, model_name_, calibrator_);
    int msg_budget = std::max(0, model_limit - sys_tokens);

    if (msg_budget <= 0) {
        result.warnings.push_back(
            "System prompt 已占用全部预算（" + std::to_string(sys_tokens) +
            " tokens），无法容纳对话历史。建议精简项目上下文或增加 max_context_tokens。");
        spdlog::error("[ContextManager] msg_budget <= 0 (sys={}, max={})",
                      sys_tokens, model_limit);
    }

    // ── 步骤 3（合并原步骤 2+3）: 实时用量检查 + 自动压缩 + 告警 ──────
    result.messages = memory.messages();
    int msg_tokens = tracker_.updateMessageTokens(result.messages, model_name_, calibrator_);
    result.total_tokens = sys_tokens + msg_tokens;

    if (model_limit > 0) {
        auto pre_check = checkThresholds(result.total_tokens);

        // 用量达到 AutoCompact 及以上 → 尝试压缩（压缩阈值独立于告警阈值，
        // 默认 95% 落在 Critical [85] 与 Error [>100] 之间，可自由配置）
        if (llm_client && auto_compact_
            && pre_check.status >= ContextStatus::AutoCompact)
        {
            spdlog::info("[ContextManager] 自动压缩触发 (用量 {}%, 阈值 {}%)",
                         pre_check.usage_percent, tracker_.autoCompactThreshold());
            auto cr = compact(memory, *llm_client, "自动压缩：上下文用量过高");
            if (cr.messages_compacted > 0) {
                // 压缩成功 — 重新计算（memory 已被 compact() 修改）
                result.messages = memory.messages();
                msg_tokens = tracker_.updateMessageTokens(result.messages, model_name_, calibrator_);
                result.total_tokens = sys_tokens + msg_tokens;
                spdlog::info("[ContextManager] 自动压缩完成: {} 条 → 摘要, 新用量 {} tokens",
                             cr.messages_compacted, result.total_tokens);
                // 重新检查（压缩可能将用量降到 Normal，避免不必要的告警）
                pre_check = checkThresholds(result.total_tokens);
            } else {
                spdlog::warn("[ContextManager] 自动压缩未产生效果: {}",
                             cr.summary.empty() ? "无可压缩消息" : cr.summary);
            }
        }

        // 基于最新 pre_check 添加告警/错误
        switch (pre_check.status) {
        case ContextStatus::Error:
            result.fatal = true;
            result.warnings.push_back(
                "致命错误：上下文用量已超过模型上限（"
                + std::to_string(pre_check.usage_percent) + "%，"
                + std::to_string(pre_check.estimated_tokens) + " / "
                + std::to_string(pre_check.model_limit)
                + " tokens）。请使用 /compact 压缩对话历史，或 /clear 清空对话后重试。");
            spdlog::error("[ContextManager] 上下文用量超限: {}% ({} / {} tokens)",
                         pre_check.usage_percent, pre_check.estimated_tokens, pre_check.model_limit);
            break;
        case ContextStatus::AutoCompact:
            result.warnings.push_back(
                "上下文用量已达 " + std::to_string(pre_check.usage_percent) +
                "%，自动压缩未生效，请手动执行 /compact。");
            spdlog::warn("[ContextManager] 自动压缩未生效，用量仍在 AutoCompact 级别: {}%",
                        pre_check.usage_percent);
            break;
        case ContextStatus::Critical:
            result.warnings.push_back(
                "上下文用量已达 " + std::to_string(pre_check.usage_percent) +
                "%，接近模型上限。建议使用 /compact 压缩对话历史。");
            spdlog::warn("[ContextManager] 上下文用量临界: {}%", pre_check.usage_percent);
            break;
        case ContextStatus::Warning:
            result.warnings.push_back(
                "上下文用量 " + std::to_string(pre_check.usage_percent) +
                "%，可考虑 /compact 释放空间。");
            break;
        default:
            break;
        }
    }

    // ── 步骤 4: 缓存到内部状态 ─────────────────────────────────────────
    last_warnings_ = result.warnings;
    tracker_.setCurrentTotalTokens(result.total_tokens);

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

    if (focus && !focus->empty()) {
        compact_prompt += std::string("\n特别注意：") + *focus;
    }

    return compact_prompt;
}

int ContextManager::estimateTokens(const std::string& text) const {
    return llm::TokenCounter::countTokensCalibrated(text, model_name_, calibrator_);
}

} // namespace agent
