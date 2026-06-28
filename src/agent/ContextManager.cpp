/// ContextManager 实现 — 增强版（会话追踪 + pin + compaction + 降级可见性）。

#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "project/IStorageBackend.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
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

/// 取项目顶层设定文件（novel.json）的最后修改时间戳。
/// 用于会话恢复时检测"项目在保存后被外部修改"→ 清空旧摘要。
/// 返回 0 表示取不到（项目路径为空或文件不存在）。
///
/// 注意：文件名必须用 ProjectIO::kNovelJsonFileName 常量，不可写字面量。
/// 此前曾写死 "project.json" 而实际文件名为 "novel.json"，
/// 导致 last_write_time 永远失败、mtime 一致性保障整条静默失效（见 DESIGN_REVIEW A5）。
/// TODO(A12): 当前只盯 novel.json 单文件，颗粒度不足——修改角色/设定时
///   characters.json 等才会变，novel.json 可能不变。后续应取多个设定文件的最新 mtime。
int64_t projectSettingsMtime(const std::string& project_path) {
    if (project_path.empty()) return 0;
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(
        project_path + "/" + ProjectIO::kNovelJsonFileName, ec);
    if (ec) return 0;
    return ftime.time_since_epoch().count();
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
    // 同步更新最近一次请求的上下文大小（API 返回的 prompt_tokens 比启发式估算更准确）。
    // usagePercent()/checkThresholds()/shouldAutoCompact() 在下一轮请求前据此判断。
    current_context_size_ = input_tokens;
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

    // 自动获取项目设定文件的修改时间（用于恢复时检测外部修改）
    const int64_t mtime = project_ ? projectSettingsMtime(project_->path) : 0;

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

    // 自动检查项目设定是否在会话保存后被外部修改过
    const int64_t current_mtime = project_ ? projectSettingsMtime(project_->path) : 0;

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
    const int64_t proj_time = projectSettingsMtime(project_->path);
    if (proj_time == 0) return false;  // novel.json 不存在或取不到
    // vectors.json 由 /index 命令生成，存储章节片段的向量嵌入。
    // 比较 novel.json 和 vectors.json 的 mtime：若项目设定在索引构建后被修改，
    // 说明向量可能引用了过期的角色/章节/设定 → 判定为 stale。
    std::error_code ec;
    auto vec_time = std::filesystem::last_write_time(project_->path + "/.novelagent/vectors.json", ec);
    if (ec) return false;  // vectors.json 不存在，可能从未执行过 /index
    return proj_time > vec_time.time_since_epoch().count();
}

// ===========================================================================
// assemble — 上下文组装核心方法
//
// 职责：
//   在每次 LLM 请求前，将三类上下文（静态项目设定、向量语义召回、对话压缩摘要）
//   与当前对话历史合并，在 max_context_tokens 预算内执行消息截断，
//   并输出 token 用量告警，最终产出完整的 ContextAssembly。
//
// 执行流程（共 8 步）：
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 0: System Prompt 构建                                  │
//   │  buildSystemPrompt(project, chapter_id)                     │
//   │  → 从 characters.json / outline.json / settings.json 渲染    │
//   │    项目级静态上下文（角色列表、大纲节点、前情提要、世界观）    │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 1: 向量检索（语义召回）                                │
//   │  @条件: vector_store_ && embedding_gen_ 均已就绪且非空       │
//   │  ┌─ skip guards ────────────────────────────────────────┐   │
//   │  │ ● vector_store_dirty_=true  → 跳过（/rewind 后脏标记）│   │
//   │  │ ● isVectorStoreStale()=true → 告警（索引比 Project 旧）│   │
//   │  └──────────────────────────────────────────────────────┘   │
//   │  ① 取对话最后一条 user 消息作为查询文本                     │
//   │  ② embedding_gen_ → 生成查询向量 query_emb                 │
//   │  ③ vector_store_->search() → 召回 top-K 最相似片段         │
//   │  ④ 格式化后追加到 system_prompt 末尾                        │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 2: 压缩摘要注入（中期记忆层）                          │
//   │  @条件: compacted_summary_ 非空（此前执行过 /compact）       │
//   │  将双层摘要（情节事实 + 风格样本）追加到 system_prompt       │
//   │  作为被压缩的旧对话的语义替代                               │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 3: Token 预算分配                                      │
//   │  sys_tokens  = countTokens(system_prompt)                   │
//   │  msg_budget = max(0, max_context_tokens - sys_tokens)      │
//   │  → 剩余预算全部留给对话消息列表                              │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 4: Token 阈值告警                                      │
//   │  checkThresholds() → 分三级状态：                            │
//   │  ● Normal  ( < 60% )  → 静默                                │
//   │  ● Warning (60%-85%)  → 建议 /compact                       │
//   │  ● Critical( ≥ 85% )  → 强烈建议 /compact                    │
//   │  ● msg_budget <= 0    → error：system prompt 独占窗口       │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 5: 对话消息截断                                        │
//   │  truncateMessages(all_msgs, msg_budget, truncated_count)    │
//   │  → 从最新消息反向贪心保留，preserved 消息优先但不免预算       │
//   │  → 安全兜底：至少保留最后一条消息（当前用户输入）             │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 6: 最终 Token 统计                                     │
//   │  total_tokens = sys_tokens + countMessages(result.messages) │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 6.5: 模型窗口超限预检（API 400 防护）                  │
//   │  total_tokens > model_context_limit → 致命告警               │
//   │  提示用户减少 /pin 数量或执行 /compact                       │
//   └──────────────────┬──────────────────────────────────────────┘
//                      ▼
//   ┌─────────────────────────────────────────────────────────────┐
//   │  步骤 7: 内部状态缓存                                        │
//   │  last_warnings_        → 供 Agent/REPL 读取                  │
//   │  last_truncated_count_ → 供外部查询截断数                    │
//   │  current_context_size_ → 更新为 total_tokens，供下次阈值检查 │
//   └─────────────────────────────────────────────────────────────┘
//
// 输出 ContextAssembly 关键字段：
//   system_prompt         — 三层上下文的拼接结果
//   messages              — 截断后的有效消息列表
//   warnings              — 所有告警的文本数组
//   total_tokens          — 最终发送的 token 总量
//   truncated_count       — 被丢弃的消息数量
//   has_compacted_context — 是否注入了压缩摘要或检索片段
// ===========================================================================

ContextAssembly ContextManager::assemble(
    const llm::Conversation& conversation,
    int max_context_tokens)
{
    ContextAssembly result;

    // ── 步骤 0: System Prompt 构建 ──────────────────────────────────────
    // 通过 PromptContextBuilder::buildForChapter() 从项目文件渲染静态上下文。
    // chapter_id 为空时回退到项目级概要（标题 + Logline + 主题）。
    if (project_) {
        result.system_prompt = buildSystemPrompt(*project_, current_chapter_id_);
    }

    // ── 步骤 1: 向量检索（语义召回）─────────────────────────────────────
    // 条件：向量存储 + 嵌入生成器均已就绪，且向量库非空。
    if (vector_store_ && embedding_gen_ && vector_store_->count() > 0) {
        // Guard 1: /rewind 脏标记 → 跳过检索（向量索引位置与当前对话不匹配）
        if (vector_store_dirty_) {
            result.warnings.push_back(
                "向量索引在 /rewind 后已标记为过期，建议 /index 重建。");
            spdlog::warn("[ContextManager] 向量索引已标记为脏，跳过检索");
            goto skip_retrieval;
        }
        // Guard 2: 向量库 mtime 比 novel.json 旧 → 告警但不阻断
        if (isVectorStoreStale()) {
            result.warnings.push_back(
                "Project 已更新，向量索引可能过期。建议 /index 重建。");
            spdlog::warn("[ContextManager] 向量索引可能过期");
        }
        // 从对话尾部反向扫描，找到最后一条用户消息作为语义查询的源文本
        const auto& msgs = conversation.messages();
        std::string last_user_text;
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
            if (it->role == llm::MessageRole::User) {
                last_user_text = it->content;
                break;
            }
        }
        if (!last_user_text.empty()) {
            try {
                // 生成查询向量 → 语义搜索 → 格式化召回结果
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
                // 检索失败不阻断主流程，让 LLM 仅依赖压缩摘要和当前对话
            }
        }
    }
    skip_retrieval:

    // ── 步骤 2: 注入压缩摘要（中期记忆层）───────────────────────────────
    // 如果之前执行过 /compact，将被压缩的旧对话摘要注入 system_prompt，
    // 作为"情节事实 + 风格样本"的语义替代，让 LLM 仍能感知被裁掉的早期对话。
    if (!compacted_summary_.empty()) {
        result.has_compacted_context = true;
        if (!result.system_prompt.empty()) result.system_prompt += "\n\n";
        result.system_prompt += "[会话历史摘要 — 当前风格参照]\n" + compacted_summary_;
    }

    // ── 步骤 3: 计算消息预算 ────────────────────────────────────────────
    // 总预算 = max_context_tokens；system_prompt 优先占用，剩余归消息列表。
    int sys_tokens = result.system_prompt.empty()
        ? 0 : llm::TokenCounter::countTokens(result.system_prompt);
    int msg_budget = std::max(0, max_context_tokens - sys_tokens);

    // ── 步骤 4: 生成 Token 用量告警 ──────────────────────────────────────
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

    // ── 步骤 5: 截断消息（支持 preserved 标记）───────────────────────────
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

    // ── 步骤 6: 统计总 token ────────────────────────────────────────────
    int msg_tokens = llm::TokenCounter::countMessages(result.messages);
    result.total_tokens = sys_tokens + msg_tokens;

    // ── 步骤 6.5: 最终预检 ──────────────────────────────────────────────
    // 在即将发送前再检查一次：总 token 是否超出模型上下文窗口上限。
    // 这是防止 LLM API 返回 400 Bad Request 的最后一道防线。
    if (token_state_.model_context_limit > 0
        && result.total_tokens > token_state_.model_context_limit) {
        result.warnings.push_back(
            "⚠ 总 token(" + std::to_string(result.total_tokens)
            + ") 超出模型窗口(" + std::to_string(token_state_.model_context_limit)
            + ")，请求可能被 API 拒绝。请减少 /pin 数量或 /compact 压缩。");
        spdlog::error("[ContextManager] 总 token({}) 超出模型窗口({})",
                      result.total_tokens, token_state_.model_context_limit);
    }

    // ── 步骤 7: 缓存到内部状态 ──────────────────────────────────────────
    // 供 Agent / REPL 层查询：last_warnings_ 用于展示给用户，
    // last_truncated_count_ 用于统计，current_context_size_ 用于下次阈值检查。
    last_warnings_ = result.warnings;
    last_truncated_count_ = result.truncated_count;
    current_context_size_ = result.total_tokens;

    return result;
}

// ===========================================================================
// buildSystemPrompt — 构建项目/章节的静态上下文
//
// 职责：
//   为 LLM 请求提供"静态上下文层"—— 即不随对话变化的那部分信息，
//   来源于 Project 文件（characters.json / outline.json / settings.json 等）。
//
// 设计模式：双层分派 + 静默回退
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │ 入口: buildSystemPrompt(project, chapter_id)                │
//   │                                                             │
//   │  ┌─ chapter_id 为空 ──────────────────────────────────────┐ │
//   │  │  模式 1: 项目级概要                                      │ │
//   │  │  输出: # 项目: {title}                                  │ │
//   │  │        Logline: {logline}                               │ │
//   │  │        主题: {theme}                                    │ │
//   │  │  用途: 无章节上下文时的降级方案                          │ │
//   │  │  (新会话 / 全局命令 / 回退路径)                          │ │
//   │  └────────────────────────────────────────────────────────┘ │
//   │                           ↓                                  │
//   │  ┌─ chapter_id 有效 ──────────────────────────────────────┐ │
//   │  │  模式 2: 章节级完整上下文                                │ │
//   │  │  委托 PromptContextBuilder::buildForChapter() 渲染      │ │
//   │  │  包含:                                                  │ │
//   │  │  ├─ 角色列表（6级优先级排序，仅保留与本章节相关的角色）     │ │
//   │  │  ├─ 大纲节点（当前章节 + 前后章节概述）                   │ │
//   │  │  ├─ 前情提要（之前已写章节的情节摘要）                    │ │
//   │  │  ├─ 世界观设定（关键设定/规则/魔法体系等）                │ │
//   │  │  └─ 写作风格指南（由 style.json 定义）                   │ │
//   │  │                                                        │ │
//   │  │  ┌─ 构建成功 ────────────────────────────────────────┐  │ │
//   │  │  │ 返回 ctx->rendered_prompt（完整渲染文本）          │  │ │
//   │  │  └──────────────────────────────────────────────────┘  │ │
//   │  │                                                        │ │
//   │  │  ┌─ 构建失败 ────────────────────────────────────────┐  │ │
//   │  │  │ 日志 WARNING + 递归回退到模式 1（项目级概要）       │  │ │
//   │  │  │ 保证调用方始终能拿到非空字符串，不阻塞流程           │  │ │
//   │  │  └──────────────────────────────────────────────────┘  │ │
//   │  └────────────────────────────────────────────────────────┘ │
//   └─────────────────────────────────────────────────────────────┘
//
// 调用者：
//   ● assemble()  — 作为"步骤 0"，构建后还会叠加向量召回和压缩摘要
//   ● compact()   — 用作项目设定参考（截断 ≤500 字节），帮助 LLM
//                    正确识别被压缩对话中的人物指代
//
// 关键约定：
//   options.task = "write_chapter" 硬编码，控制 PromptContextBuilder
//   内部的筛选逻辑（如角色按"与本章节写作任务的相关性"排序）。
//   若将来增加其他任务类型（如 rewrite / outline_plan），
//   需在此处根据上下文切换 task 值。
// ===========================================================================

std::string ContextManager::buildSystemPrompt(
    const Project& project,
    const std::string& chapter_id)
{
    // ── 模式 1: 项目级概要 ────────────────────────────────────────────────
    // 当 chapter_id 为空时（例如全局命令、新会话尚未选择章节），
    // 仅输出项目最基础的标识信息，不涉及任何章节级细节。
    if (chapter_id.empty()) {
        std::string prompt;
        prompt += "# 项目: " + project.title + "\n";
        if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
        if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";
        return prompt;
    }

    // ── 模式 2: 章节级完整上下文 ──────────────────────────────────────────
    // 通过 PromptContextBuilder 渲染完整的章节写作上下文。
    // task = "write_chapter" 指示构建器按"写作任务"的标准筛选和排序内容。
    prompt::PromptContextOptions options;
    options.task = "write_chapter";
    options.chapter_id = chapter_id;

    auto ctx = prompt::PromptContextBuilder::buildForChapter(project, options);
    if (!ctx) {
        // 构建失败（如章节文件损坏/不存在）→ 静默回退到模式 1
        // 不做 hard fail，保证调用方（assemble/compact）始终获得有效字符串
        spdlog::warn("[ContextManager] 章节 '{}' 上下文构建失败，回退", chapter_id);
        return buildSystemPrompt(project);
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
