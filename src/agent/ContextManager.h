#pragma once

// 上下文管理器（增强版 — 会话级追踪 + pin 保留 + 手动 compaction）。
//
// Issue 3 拆分后：TokenTracker（③ Token 追踪）+ Compactor（②⑤ 对话压缩）
// 已抽为独立类。ContextManager 保留核心职责作为门面：
//   1. 构建动态 system prompt（项目/章节上下文 + 压缩摘要）
//   3. (委托 TokenTracker)  会话级 token 追踪（累计输入/输出，阈值检查）
//   4. 向量检索协调（IVectorStore + IEmbeddingGenerator）
//   5. (委托 Compactor)    手动/自动 compaction（LLM 对话压缩）
//   6. 会话持久化委托给 SessionPersistence
//
// 依赖：通过 FileStorageBackend 访问存储（封装项目路径 + 转发 ProjectIO）。

#include "agent/Compactor.h"
#include "agent/ContextManagerTypes.h"
#include "agent/SessionPersistence.h"
#include "agent/TokenTracker.h"
#include "llm/Conversation.h"

#include <optional>
#include <string>
#include <vector>

// 前向声明
struct Project;
class FileStorageBackend;

namespace llm {
class ILLMClient;
}

namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}

namespace agent {

// 上下文管理器。
class ContextManager {
public:
    // 默认构造函数（不使用持久化存储）。
    ContextManager();

    // 构造函数注入存储后端。
    // @param storage 文件存储后端（封装项目路径，转发 ProjectIO）
    explicit ContextManager(FileStorageBackend& storage);

    // ================================================================
    // 核心入口
    // ================================================================

    // 组装上下文 — 一站式入口。
    //
    // 使用内部存储的 project_ 和 current_chapter_id_（由 setProject/setCurrentChapter 设置）。
    //
    // 7 步流水线：
    //   1. 构建系统提示词（项目上下文 + 当前章节的角色/大纲/世界观）
    //   1.5. 向量检索（取最后一条 user 消息做语义召回，注入"仅作事实参考"标签）
    //   2. 注入压缩摘要（如果 compact() 已执行，"当前风格参照"标签）
    //   3. 计算消息预算 = max_context_tokens - system_prompt_tokens
    //   4. 生成告警（用量临界 / 预算耗尽 / 向量过期 / 超出窗口）
    //   5. 截断消息（preserved 优先保留，最新消息贪心保留）
    //   6. 统计总 token + 最终预检（超出模型窗口则追加警告）
    //   7. 缓存警告/截断数/当前大小（供 Agent/REPL 在下一次请求前读取）
    ContextAssembly assemble(
        const llm::Conversation& conversation,
        int max_context_tokens);

    // 构建系统提示词（委托 PromptContextBuilder）。
    std::string buildSystemPrompt(const Project& project,
                                   const std::string& chapter_id = "");

    // ================================================================
    // Project 注入（供 assemble 使用）
    // ================================================================

    // 设置当前项目（非拥有指针，生命周期由 NovelAgentApp 管理）。
    void setProject(const Project* p) { project_ = p; }

    // 设置当前活跃章节 ID（供 assemble 构建章节上下文）。
    void setCurrentChapter(const std::string& id) { current_chapter_id_ = id; }

    // ================================================================
    // 会话级 Token 追踪（委托 TokenTracker）
    // ================================================================

    // 设置模型上下文窗口上限（从 ProviderConfig 获取）。
    void setModelContextLimit(int limit) {
        tracker_.setModelLimit(limit);
        compactor_.setModelContextLimit(limit);  // MED-1: 同步到 compactor 做 token 预算保护
    }

    // 累计一次请求的 token 消耗。
    void recordUsage(int input_tokens, int output_tokens) { tracker_.record(input_tokens, output_tokens); }

    // 请求前检查上下文用量状态。
    PreRequestResult checkThresholds() const { return tracker_.check(); }

    // 返回累计统计。
    SessionTokenState sessionStats() const { return tracker_.snapshot(); }

    // 返回当前用量百分比 [0, 100]。
    int usagePercent() const { return tracker_.usagePercent(); }

    // 重置会话统计（/clear 时调用）。
    void resetSession();

    // ================================================================
    // Compaction（中期记忆层 — 委托 Compactor）
    // ================================================================

    // 执行对话压缩 — 用 LLM 将旧消息摘要为一段文本。
    CompactResult compact(
        llm::Conversation& conversation,
        llm::ILLMClient& llm_client,
        std::optional<std::string> focus = std::nullopt);

    // 当前是否有压缩摘要。
    bool hasCompactedSummary() const { return compactor_.hasSummary(); }

    // 清除压缩摘要。
    void clearCompactedSummary() { compactor_.clear(); }

    // 返回压缩标记位（被 compact() 压缩的消息数，0 = 无压缩）。
    int compactionMarker() const { return compactor_.marker(); }

    // 设置自动 compaction（达到阈值自动触发）。
    void setAutoCompact(bool enabled, int threshold_pct = 70);
    bool shouldAutoCompact() const;

    // 返回最后一次 assemble() 产生的警告列表。
    const std::vector<std::string>& lastWarnings() const { return last_warnings_; }

    // 返回最后一次 assemble() 截断的消息数。
    int lastTruncatedCount() const { return last_truncated_count_; }

    // ================================================================
    // 向量检索（长期记忆层）
    // ================================================================

    // 设置向量检索后端（非拥有指针，生命周期由 NovelAgentApp 管理）。
    void setRetrievalBackend(retrieval::IVectorStore* store,
                             retrieval::IEmbeddingGenerator* gen,
                             int top_k = 3);

    // 是否有可用的检索后端。
    bool hasRetrievalBackend() const { return vector_store_ && embedding_gen_; }

    // 在检索前检查 Project 是否比向量库更新（在 assemble 内部调用）。
    bool isVectorStoreStale() const;

    // /rewind 后标记向量库为脏，下次检索前提示 /index 重建。
    void clearVectorStore() { vector_store_dirty_ = true; }

    // ================================================================
    // 会话持久化（委托 SessionPersistence）
    // ================================================================

    SessionPersistence& persistence() { return persistence_; }

    void saveSession(const llm::Conversation& conv) { persistence_.save(conv); }
    llm::Conversation loadSession() { return persistence_.load(); }
    void archiveSession(const llm::Conversation& conv) { persistence_.archive(conv); }

    // 保存完整会话状态（对话 + 元数据），供灾难恢复。
    // project_mtime 自动从 project_ 获取。
    void saveSessionState(const llm::Conversation& conv,
                          const std::string& chapter_id,
                          const std::vector<size_t>& preserved_indices);

    // 加载完整会话状态并恢复到 ContextManager 内部状态。
    // 自动对比 project_mtime，如果 Project 被修改过则清空压缩摘要。
    void loadSessionState(llm::Conversation& conv,
                          std::string& out_chapter_id);

    // 公开子组件（只读访问，供测试/诊断/Agent 直接使用）
    const TokenTracker& tracker() const { return tracker_; }
    const Compactor& compactor() const { return compactor_; }

private:
    FileStorageBackend& storage_;
    SessionPersistence persistence_;

    // Issue 3: 拆分为 TokenTracker + Compactor
    TokenTracker tracker_;
    Compactor compactor_;

    // ── Project 注入（非拥有）──
    const Project* project_ = nullptr;
    std::string current_chapter_id_;

    // ── 会话级状态（精简后）──
    std::vector<std::string> last_warnings_;  //  最后一次 assemble() 的警告缓存
    int last_truncated_count_ = 0;            //  最后一次 assemble() 的截断数

    // ── 向量检索后端（非拥有）──
    retrieval::IVectorStore* vector_store_ = nullptr;
    retrieval::IEmbeddingGenerator* embedding_gen_ = nullptr;
    int retrieval_top_k_ = 3;
    bool vector_store_dirty_ = false;  // /rewind 后标记，跳过检索并提示 /index

    // 按 token 预算从新到旧截断消息（preserved 消息优先保留）。
    static std::vector<llm::Message> truncateMessages(
        const std::vector<llm::Message>& messages,
        int budget,
        int& truncated_count);
};

} // namespace agent
