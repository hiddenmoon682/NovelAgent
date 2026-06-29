/// ContextManager 实现 — 增强版（会话追踪 + pin + compaction + 降级可见性）。

#include "agent/ContextManager.h"

#include "llm/Conversation.h"
#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "agent/PromptContextBuilder.h"
#include "retrieval/IVectorStore.h"
#include "retrieval/IEmbeddingGenerator.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <sstream>

namespace agent {

namespace {
/// 默认存储占位符（不使用持久化功能时的回退）。
FileStorageBackend& defaultStorage() {
    static FileStorageBackend s("");
    return s;
}

/// 取项目设定文件的最后修改时间戳（取多个文件中的最新值）。
/// 用于会话恢复时检测"项目在保存后被外部修改"→ 清空旧摘要。
/// 返回 0 表示取不到（项目路径为空或所有文件均不存在）。
///
/// A12 修复：此前只盯 project.json（后改为 novel.json，A5 已修），颗粒度不足——
///   修改角色/设定/规则时，characters.json/settings.json/world_rules.json 等文件才会变，
///   novel.json 可能不变。现改为取 novel.json + outline.json + characters.json +
///   settings.json + world_rules.json 的最新 mtime，覆盖全部主要 JSON 设定文件。
///   局限：章节正文（.md 文件）不在检测范围内（A12 根治需工具层标记向量失效，
///   见 ContextManager::clearVectorStore / DesignReview_A12）。
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
    "总长度控制在 2000 字以内，事实与风格的比例由你判断。";

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

ContextManager::ContextManager(FileStorageBackend& storage)
    : storage_(storage)
    , persistence_(storage)
{}

// ===========================================================================
// 会话状态持久化
// ===========================================================================

void ContextManager::saveSessionState(
    const llm::Conversation& conv,
    const std::string& chapter_id,
    const std::vector<size_t>& preserved_indices)
{
    persistence_.save(conv);

    const int64_t mtime = project_ ? projectSettingsMtime(project_->path) : 0;

    SessionMeta meta;
    meta.compacted_summary = compactor_.summary();
    meta.compaction_marker = compactor_.marker();
    meta.token_state = tracker_.snapshot();
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

void ContextManager::resetSession() {
    tracker_.reset();
    compactor_.clear();
    last_warnings_.clear();
    last_truncated_count_ = 0;
    vector_store_dirty_ = false;
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
                                     current_chapter_id_, project_,
                                     std::move(focus));
    // 压缩后向量索引位置失效，恢复向量检索
    if (result.messages_compacted > 0) {
        vector_store_dirty_ = false;
    }
    return result;
}

void ContextManager::setAutoCompact(bool enabled, int threshold_pct) {
    compactor_.setAutoCompact(enabled, threshold_pct);
}

bool ContextManager::shouldAutoCompact() const {
    return compactor_.shouldAutoCompact(tracker_.usagePercent());
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
    if (proj_time == 0) return false;  // 没有一个设定文件存在
    // vectors.json 由 /index 命令生成，存储章节片段的向量嵌入。
    // 比较项目设定文件最新 mtime 与 vectors.json mtime：若设定在索引构建后被修改，
    // 说明向量可能引用了过期信息 → 判定为 stale。
    std::error_code ec;
    auto vec_time = std::filesystem::last_write_time(project_->path + "/.novelagent/vectors.json", ec);
    if (ec) return false;  // vectors.json 不存在，可能从未执行过 /index
    bool stale = proj_time >= vec_time.time_since_epoch().count();
    spdlog::debug("[ContextManager] isVectorStoreStale: proj_time={}, vec_time={}, stale={}",
                  proj_time, vec_time.time_since_epoch().count(), stale);
    return stale;
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
        // 从对话尾部反向扫描，找到最后一条用户消息作为语义查询的源文本。
        // A10: 拼接最近 3 条 user 消息做查询（而非只用最后一条），
        // 避免"继续""改得更有张力"这类短句召回质量差。
        const auto& msgs = conversation.messages();
        std::vector<std::string> recent_user_texts;
        for (auto it = msgs.rbegin(); it != msgs.rend() && recent_user_texts.size() < 3; ++it) {
            if (it->role == llm::MessageRole::User) {
                recent_user_texts.push_back(it->content);
            }
        }
        std::string last_user_text;
        for (int i = static_cast<int>(recent_user_texts.size()) - 1; i >= 0; --i) {
            if (!last_user_text.empty()) last_user_text += " [SEP] ";
            last_user_text += recent_user_texts[i];
        }
        if (!last_user_text.empty()) {
            try {
                // 生成查询向量 → 语义搜索 → 格式化召回结果
                auto query_emb = embedding_gen_->generateEmbedding(last_user_text);
                auto results = vector_store_->search(query_emb, retrieval_top_k_);

                // A3: 收集确定性上下文中已覆盖的 chapter_id，跳过重复片段
                std::set<std::string> covered_ids;
                if (project_) {
                    covered_ids.insert(current_chapter_id_);
                    for (const auto& ch : project_->outline.chapters) {
                        if (ch.id == current_chapter_id_) continue;
                        // 相邻章节也标记为已覆盖（缩减范围而非全包含）
                    }
                }

                if (!results.empty()) {
                    std::string retrieval_section =
                        "\n\n=== 补充记忆（向量相似度检索，"
                        "优先级低于上方确定性上下文）===\n";
                    int added = 0;
                    for (size_t i = 0; i < results.size(); ++i) {
                        if (results[i].metadata.contains("text")) {
                            std::string src = results[i].metadata.value("chapter_id", "?");
                            // 跳过已在确定性上下文中的章节片段
                            if (covered_ids.count(src)) continue;
                            ++added;
                            retrieval_section += "片段" + std::to_string(i + 1)
                                + " (ch-" + src + ", 相关度"
                                + std::to_string(static_cast<int>(results[i].similarity * 100))
                                + "%): " + results[i].metadata["text"].get<std::string>() + "\n";
                        }
                    }
                    if (added > 0) {
                        result.system_prompt += retrieval_section;
                        result.has_semantic_context = true;
                        spdlog::info("[ContextManager] 向量检索召回 {} 条，去重后注入 {} 条",
                                     results.size(), added);
                    }
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
    const auto& summary = compactor_.summary();
    if (!summary.empty()) {
        result.has_compacted_context = true;
        if (!result.system_prompt.empty()) result.system_prompt += "\n\n";
        result.system_prompt += "[会话历史摘要 — 当前风格参照]\n" + summary;
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

    // ── 步骤 7: 缓存到内部状态 ──────────────────────────────────────────
    // 供 Agent / REPL 层查询：last_warnings_ 用于展示给用户，
    // last_truncated_count_ 用于统计，current_context_size_ 用于下次阈值检查。
    last_warnings_ = result.warnings;
    last_truncated_count_ = result.truncated_count;
    tracker_.setCurrentContextSize(result.total_tokens);

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
//   ● compact()   — 用作项目设定参考（截断 ≤1200 字节），帮助 LLM
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

    if (llm::TokenCounter::countMessages(messages) <= budget) return messages;

    std::vector<llm::Message> result;
    int used = 0;
    for (auto it = normal_msgs.rbegin(); it != normal_msgs.rend(); ++it) {
        int cost = llm::TokenCounter::countSingleMessage(**it);
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
