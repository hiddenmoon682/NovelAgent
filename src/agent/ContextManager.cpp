/// ContextManager 实现 — 增强版（会话追踪 + pin + compaction + 降级可见性）。

#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "project/IStorageBackend.h"
#include "project/Models.h"
#include "agent/PromptContextBuilder.h"
#include "retrieval/IVectorStore.h"
#include "retrieval/IEmbeddingGenerator.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace agent {

namespace {
/// 默认存储占位符（不使用持久化功能时的回退）。
IStorageBackend& defaultStorage() {
    static FileStorageBackend s("");
    return s;
}

/// Compaction 时保留的最近消息对数。
constexpr int kCompactKeepExchanges = 10;   // 保留最近 10 对 = ~20 条消息

/// Compaction 用的 system prompt — 双层摘要：情节事实 + 风格样本。
constexpr const char* kCompactSystemPrompt =
    "你是一个小说创作助手的上下文压缩器。用中文对以下对话历史进行双层摘要：\n"
    "\n"
    "1. 情节事实：角色决策与性格变化、情节转折与关键事件、\n"
    "   世界观设定变更、未解决的伏笔与冲突、待完成任务与下一步计划\n"
    "\n"
    "2. 风格参考：摘录 2-3 句最能代表当前写作风格的原句——\n"
    "   保留其修辞手法、句式节奏、情绪氛围和对话语气\n"
    "\n"
    "总长度控制在 500 字以内，事实与风格的比例由你判断。";

/// Token 用量告警阈值。
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

ContextManager::ContextManager(IStorageBackend& storage)
    : storage_(storage)
    , persistence_(storage)
{}

// ===========================================================================
// 会话级 Token 追踪
// ===========================================================================

void ContextManager::setModelContextLimit(int limit) {
    if (limit > 0) token_state_.model_context_limit = limit;
}

void ContextManager::recordUsage(int input_tokens, int output_tokens) {
    token_state_.total_input_tokens += input_tokens;
    token_state_.total_output_tokens += output_tokens;
    token_state_.request_count++;
}

PreRequestResult ContextManager::checkThresholds() const {
    PreRequestResult r;
    r.model_limit = token_state_.model_context_limit;
    // 用最后一次请求的实际上下文大小（非累计值），避免累计放大后恒 true
    r.estimated_tokens = current_context_size_;
    if (r.model_limit > 0) {
        r.usage_percent = (r.estimated_tokens * 100) / r.model_limit;
    }

    if (r.usage_percent >= kCriticalPercent) {
        r.status = ContextStatus::Critical;
    } else if (r.usage_percent >= kWarnPercent) {
        r.status = ContextStatus::Warning;
    }
    return r;
}

SessionTokenState ContextManager::sessionStats() const {
    return token_state_;
}

int ContextManager::usagePercent() const {
    if (token_state_.model_context_limit <= 0) return 0;
    return (current_context_size_ * 100) / token_state_.model_context_limit;
}

void ContextManager::resetSession() {
    token_state_ = SessionTokenState{};
    clearCompactedSummary();
    last_warnings_.clear();
    last_truncated_count_ = 0;
    vector_store_dirty_ = false;
    current_context_size_ = 0;
}

// ===========================================================================
// 会话状态持久化
// ===========================================================================

void ContextManager::saveSessionState(
    const llm::Conversation& conv,
    const std::string& chapter_id,
    const std::vector<size_t>& preserved_indices)
{
    persistence_.save(conv);

    // 自动获取 project.json 的修改时间
    int64_t mtime = 0;
    if (project_ && !project_->path.empty()) {
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(project_->path + "/project.json", ec);
        if (!ec) mtime = ftime.time_since_epoch().count();
    }

    SessionMeta meta;
    meta.compacted_summary = compacted_summary_;
    meta.compaction_marker = compaction_marker_;
    meta.token_state = token_state_;
    meta.last_chapter_id = chapter_id;
    meta.preserved_indices = preserved_indices;
    meta.project_mtime = mtime;
    meta.vector_store_dirty = vector_store_dirty_;
    persistence_.saveMeta(meta);

    spdlog::info("[ContextManager] 完整会话状态已保存 (mtime={})", mtime);
}

void ContextManager::loadSessionState(
    llm::Conversation& conv,
    std::string& out_chapter_id)
{
    conv = persistence_.load();
    auto meta = persistence_.loadMeta();

    // 自动检查 Project 是否在保存后被修改过
    int64_t current_mtime = 0;
    if (project_ && !project_->path.empty()) {
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(project_->path + "/project.json", ec);
        if (!ec) current_mtime = ftime.time_since_epoch().count();
    }

    if (current_mtime > 0 && meta.project_mtime > 0
        && current_mtime != meta.project_mtime) {
        spdlog::warn("[ContextManager] Project 设定已变更 (mtime {} → {})，清空旧摘要",
                     meta.project_mtime, current_mtime);
        meta.compacted_summary.clear();
        meta.compaction_marker = 0;
    }

    // 恢复到内部状态
    compacted_summary_ = meta.compacted_summary;
    compaction_marker_ = meta.compaction_marker;
    token_state_ = meta.token_state;
    vector_store_dirty_ = meta.vector_store_dirty;
    out_chapter_id = meta.last_chapter_id;

    // 恢复 preserved 标记
    for (auto idx : meta.preserved_indices) {
        if (idx < conv.size()) {
            const_cast<llm::Message&>(conv.all()[idx]).preserved = true;
        }
    }

    spdlog::info("[ContextManager] 完整会话状态已恢复 (消息={}, preserved={}, compact={}, requests={})",
                 conv.size(), meta.preserved_indices.size(),
                 !meta.compacted_summary.empty(), meta.token_state.request_count);
}

void ContextManager::setAutoCompact(bool enabled, int threshold_pct) {
    auto_compact_ = enabled;
    if (threshold_pct > 0 && threshold_pct <= 100) {
        auto_compact_threshold_ = threshold_pct;
    }
}

bool ContextManager::shouldAutoCompact() const {
    if (!auto_compact_) return false;
    if (token_state_.model_context_limit <= 0) return false;
    int pct = (current_context_size_ * 100) / token_state_.model_context_limit;
    return pct >= auto_compact_threshold_;
}

// ===========================================================================
// 向量检索
// ===========================================================================

void ContextManager::setRetrievalBackend(
    retrieval::IVectorStore* store,
    retrieval::IEmbeddingGenerator* gen,
    int top_k)
{
    // 三个指针均为非拥有，生命周期由 NovelAgentApp 管理。
    // EmbeddingGenerator 将用户查询文本转为向量，IVectorStore 执行语义相似度搜索。
    vector_store_ = store;
    embedding_gen_ = gen;
    retrieval_top_k_ = top_k > 0 ? top_k : 3;  // 默认召回 top-3 最相似章节片段
}

bool ContextManager::isVectorStoreStale() const {
    if (!project_ || project_->path.empty()) return false;
    std::error_code ec;
    auto proj_time = std::filesystem::last_write_time(project_->path + "/project.json", ec);
    if (ec) return false;
    // vectors.json 由 /index 命令生成，存储章节片段的向量嵌入。
    // 比较 project.json 和 vectors.json 的 mtime：若 Project 在索引构建后被修改，
    // 说明向量可能引用了过期的角色/章节/设定 → 判定为 stale。
    auto vec_time = std::filesystem::last_write_time(project_->path + "/.novelagent/vectors.json", ec);
    if (ec) return false;  // vectors.json 不存在，可能从未执行过 /index
    return proj_time > vec_time;
}

// ===========================================================================
// assemble — 增强版
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int max_context_tokens)
{
    ContextAssembly result;

    // 1. 构建 system prompt（使用内部 project_ 和 current_chapter_id_）
    if (project_) {
        result.system_prompt = buildSystemPrompt(*project_, current_chapter_id_);
    }

    // 1.5. 向量检索（取最后一条 user 消息做语义召回）
    if (vector_store_ && embedding_gen_ && vector_store_->count() > 0) {
        // /rewind 后标记为脏 → 跳过检索
        if (vector_store_dirty_) {
            result.warnings.push_back(
                "向量索引在 /rewind 后已标记为过期，建议 /index 重建。");
            spdlog::warn("[ContextManager] 向量索引已标记为脏，跳过检索");
            goto skip_retrieval;
        }
        // 检查向量库是否比 Project 旧
        if (isVectorStoreStale()) {
            result.warnings.push_back(
                "Project 已更新，向量索引可能过期。建议 /index 重建。");
            spdlog::warn("[ContextManager] 向量索引可能过期");
        }
        const auto& msgs = conversation.messages();
        // 找最后一条 user 消息
        std::string last_user_text;
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
            if (it->role == llm::MessageRole::User) {
                last_user_text = it->content;
                break;
            }
        }
        if (!last_user_text.empty()) {
            try {
                auto query_emb = embedding_gen_->generateEmbedding(last_user_text);
                auto results = vector_store_->search(query_emb, retrieval_top_k_);
                if (!results.empty()) {
                    std::string retrieval_section = "\n\n[相关历史片段 — 仅作事实参考，风格以当前上下文为准]\n";
                    for (size_t i = 0; i < results.size(); ++i) {
                        if (results[i].metadata.contains("text")) {
                            std::string src = results[i].metadata.value("chapter_id", "?");
                            retrieval_section += "片段" + std::to_string(i + 1)
                                + " (ch-" + src + ", 相似度"
                                + std::to_string(static_cast<int>(results[i].similarity * 100))
                                + "%): " + results[i].metadata["text"].get<std::string>() + "\n";
                        }
                    }
                    result.system_prompt += retrieval_section;
                    result.has_compacted_context = true;
                    spdlog::info("[ContextManager] 向量检索召回 {} 条片段", results.size());
                }
            } catch (const std::exception& e) {
                spdlog::warn("[ContextManager] 向量检索失败: {}", e.what());
                // 检索失败不阻断主流程
            }
        }
    }
    skip_retrieval:

    // 2. 注入压缩摘要（如果存在）
    if (!compacted_summary_.empty()) {
        result.has_compacted_context = true;
        if (!result.system_prompt.empty()) result.system_prompt += "\n\n";
        result.system_prompt += "[会话历史摘要 — 当前风格参照]\n" + compacted_summary_;
    }

    // 3. 计算消息预算
    int sys_tokens = result.system_prompt.empty()
        ? 0 : llm::TokenCounter::countTokens(result.system_prompt);
    int msg_budget = std::max(0, max_context_tokens - sys_tokens);

    // 4. 生成告警
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

    if (msg_budget <= 0) {
        result.warnings.push_back(
            "System prompt 已占用全部预算（" + std::to_string(sys_tokens) +
            " tokens），无法容纳对话历史。建议精简项目上下文或增加 max_context_tokens。");
        spdlog::error("[ContextManager] msg_budget <= 0 (sys={}, max={})",
                      sys_tokens, max_context_tokens);
    }

    // 5. 截断消息（支持 preserved 标记）
    const auto& all_msgs = conversation.messages();
    result.messages = truncateMessages(all_msgs, msg_budget, result.truncated_count);

    if (result.truncated_count > 0) {
        result.warnings.push_back(
            "对话历史已截断 " + std::to_string(result.truncated_count) +
            " 条消息，旧内容可能丢失。使用 /compact 可压缩保留关键信息。");
        spdlog::warn("[ContextManager] 截断 {} 条消息 (预算={} sys={} msg_budget={})",
                     result.truncated_count, max_context_tokens, sys_tokens, msg_budget);
    }

    // 6. 统计总 token
    int msg_tokens = llm::TokenCounter::countMessages(result.messages);
    result.total_tokens = sys_tokens + msg_tokens;

    // 6.5. 最终预检：总 token 是否超出模型窗口（防止 API 400 错误）
    if (token_state_.model_context_limit > 0
        && result.total_tokens > token_state_.model_context_limit) {
        result.warnings.push_back(
            "⚠ 总 token(" + std::to_string(result.total_tokens)
            + ") 超出模型窗口(" + std::to_string(token_state_.model_context_limit)
            + ")，请求可能被 API 拒绝。请减少 /pin 数量或 /compact 压缩。");
        spdlog::error("[ContextManager] 总 token({}) 超出模型窗口({})",
                      result.total_tokens, token_state_.model_context_limit);
    }

    // 7. 缓存（供 Agent/REPL 读取）
    last_warnings_ = result.warnings;
    last_truncated_count_ = result.truncated_count;
    current_context_size_ = result.total_tokens;  // 用于阈值检查

    return result;
}

// ===========================================================================
// buildSystemPrompt — 构建项目/章节的静态上下文
//
// 两种模式（由 chapter_id 区分）：
//   1. chapter_id 为空 → 返回项目级概要（标题、Logline、主题），用于无章节上下文时
//   2. chapter_id 有效 → 通过 PromptContextBuilder::buildForChapter() 构建完整上下文
//      包含：角色列表（6级优先级排序）、大纲节点、前情提要、世界观设定等
//   构建失败时回退到模式 1。
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project,
    const std::string& chapter_id)
{
    if (chapter_id.empty()) {
        std::string prompt;
        prompt += "# 项目: " + project.title + "\n";
        if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
        if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";
        return prompt;
    }

    prompt::PromptContextOptions options;
    options.task = "write_chapter";
    options.chapter_id = chapter_id;

    auto ctx = prompt::PromptContextBuilder::buildForChapter(project, options);
    if (!ctx) {
        spdlog::warn("[ContextManager] 章节 '{}' 上下文构建失败，回退", chapter_id);
        return buildSystemPrompt(project);  // 回退到项目级概要
    }

    return ctx->rendered_prompt;
}

// ===========================================================================
// compact — LLM 驱动的对话压缩（中期记忆层）
//
// 流程：
//   1. 边界判定 — 消息数 ≤ kCompactKeepExchanges*2 时跳过（消息不足）
//   2. 消息提取 — 取前 N-K 条消息拼接为角色标注文本 [用户]/[助手]/[工具]
//   3. Prompt 拼接 — 双层摘要指令 + 项目设定参考（截断 ≤500 字节） + 可选焦点
//   4. LLM 调用 — 非流式生成摘要（情节事实 + 风格样本，≤500 字）
//   5. 状态更新 — 存入 compacted_summary_ + 设置 compaction_marker_
//      + 清除 vector_store_dirty_（压缩后对话已重排）
//
// 副作用：compacted_summary_（后续 assemble() 注入）、compaction_marker_
//   （/rewind 跨边界检测）、vector_store_dirty_ = false（恢复向量检索）
// ===========================================================================

CompactResult ContextManager::compact(
    const llm::Conversation& conversation,
    llm::ILLMClient& llm_client,
    std::optional<std::string> focus)
{
    CompactResult result;
    const auto& all_msgs = conversation.all();
    int total_msgs = static_cast<int>(all_msgs.size());

    // 计算保留边界：保留最后 kCompactKeepExchanges * 2 条消息
    int keep_count = kCompactKeepExchanges * 2;
    if (total_msgs <= keep_count) {
        spdlog::info("[ContextManager] compact 跳过: 消息不足 ({} 条)", total_msgs);
        result.summary = "(消息数量不足，无需压缩)";
        return result;
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
            case llm::MessageRole::User:      role_str = "用户"; break;
            case llm::MessageRole::Assistant:  role_str = "助手"; break;
            case llm::MessageRole::Tool:       role_str = "工具"; break;
            default: continue;
        }
        oss << "[" << role_str << "] " << msg.content << "\n";
    }
    std::string conversation_text = oss.str();

    // 构建 compaction 请求
    std::string compact_prompt = kCompactSystemPrompt;
    // 注入当前角色/设定上下文，帮助 LLM 正确识别人物指代
    if (project_) {
        std::string ctx = buildSystemPrompt(*project_, current_chapter_id_);
        if (!ctx.empty()) {
            if (ctx.size() > 500) {
                size_t trunc_len = 500;
                // 不在 UTF-8 多字节字符中间截断：
                // UTF-8 续字节（0x80-0xBF）不能独立存在，向前回退到字符边界
                while (trunc_len > 0 && (static_cast<unsigned char>(ctx[trunc_len]) & 0xC0) == 0x80) {
                    --trunc_len;
                }
                ctx = ctx.substr(0, trunc_len) + "...";
            }
            compact_prompt += "\n\n当前项目设定参考（用于正确识别人物指代）：\n" + ctx;
        }
    }
    if (focus && !focus->empty()) {
        compact_prompt += std::string("\n特别注意：") + *focus;
    }

    std::vector<llm::Message> compact_msgs = {
        llm::Message::user(conversation_text)
    };

    try {
        // 非流式调用 LLM 生成摘要
        auto response = llm_client.chatNonStreaming(compact_msgs, {}, compact_prompt);
        result.summary = response.content;
        result.tokens_after = llm::TokenCounter::countTokens(result.summary);

        // 存储摘要 + 标记
        compacted_summary_ = result.summary;
        compaction_marker_ = compact_count;  // 被压缩的消息数，/rewind 检测用
        // 压缩后对话历史已重新整理，向量索引对应的旧消息位置不再有效。
        // 清除脏标记以恢复向量检索（下次 assemble() 基于新消息位置进行语义召回）。
        vector_store_dirty_ = false;

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

// ===========================================================================
// truncateMessages — 支持 preserved 标记
// ===========================================================================

std::vector<llm::Message> ContextManager::truncateMessages(
    const std::vector<llm::Message>& messages,
    int budget,
    int& truncated_count)
{
    truncated_count = 0;
    if (messages.empty()) return messages;

    // 第一遍：分离 preserved 和普通消息
    std::vector<llm::Message> preserved_msgs;
    std::vector<const llm::Message*> normal_msgs;
    for (const auto& msg : messages) {
        if (msg.preserved) {
            preserved_msgs.push_back(msg);
        } else {
            normal_msgs.push_back(&msg);
        }
    }

    // preserved 消息计入预算（给优先级，但不免预算）
    int preserved_tokens = llm::TokenCounter::countMessages(preserved_msgs);
    int remaining_budget = budget - preserved_tokens;

    // 如果 preserved 已超预算，保留所有 preserved 但发出警告
    if (remaining_budget < 0) {
        spdlog::warn("[ContextManager] preserved 消息已占 {} tokens，超出预算 {} tokens。"
                     "建议取消部分 pin 或增加 max_context_tokens。",
                     preserved_tokens, -remaining_budget);
        truncated_count = static_cast<int>(normal_msgs.size());
        // P0 修复：强制保留最后一条普通消息（当前用户输入），不计预算
        if (!normal_msgs.empty()) {
            preserved_msgs.push_back(*normal_msgs.back());
            --truncated_count;
        }
        return preserved_msgs;
    }

    // 如果所有消息都在预算内，直接返回原始顺序
    if (llm::TokenCounter::countMessages(messages) <= budget) {
        return messages;
    }

    // 第二遍：对普通消息从最新反向贪心保留
    std::vector<llm::Message> result;
    int used = 0;
    for (auto it = normal_msgs.rbegin(); it != normal_msgs.rend(); ++it) {
        int cost = llm::TokenCounter::countSingleMessage(**it);
        if (used + cost > remaining_budget) break;
        used += cost;
        result.push_back(**it);
    }
    std::reverse(result.begin(), result.end());

    // 将 preserved 消息插入结果前面
    for (auto it = preserved_msgs.rbegin(); it != preserved_msgs.rend(); ++it) {
        result.insert(result.begin(), *it);
    }

    truncated_count = static_cast<int>(messages.size()) - static_cast<int>(result.size());

    // 安全兜底：如果结果为空，至少保留最后一条
    if (result.empty() && !messages.empty()) {
        result.push_back(messages.back());
        --truncated_count;
        spdlog::warn("[ContextManager] 预算严重不足，仅保留最后一条消息");
    }
    return result;
}

} // namespace agent
