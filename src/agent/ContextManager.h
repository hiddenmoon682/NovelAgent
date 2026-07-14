#pragma once

// 上下文管理器 — 会话级追踪 + pin 保留 + compaction + 持久化。
//
// 职责：
//   1. 构建动态 system prompt（项目上下文）
//   2. (委托 TokenTracker)  会话级 token 追踪（累计输入/输出，阈值检查）
//   3. 对话压缩（LLM 驱动的摘要压缩，将旧消息压缩为情节事实+风格参考）
//   4. 会话持久化委托给 SessionPersistence
//
// 依赖：通过 FileStorageBackend 访问存储（封装项目路径 + 转发 ProjectIO）。

#include "agent/ContextManagerTypes.h"
#include "agent/SessionPersistence.h"
#include "agent/TokenTracker.h"
#include "llm/Conversation.h"
#include "llm/TokenCounter.h"

#include <optional>
#include <string>
#include <vector>

// 前向声明
struct Project;
class FileStorageBackend;

namespace llm {
class ILLMClient;
}

namespace agent {

// 上下文管理器。
class ContextManager {
public:
    // 默认构造函数（不使用持久化存储）。
    ContextManager();

    // 构造函数注入存储后端。
    // storage 文件存储后端（封装项目路径，转发 ProjectIO）
    explicit ContextManager(FileStorageBackend& storage);

    // ================================================================
    // 核心入口
    // ================================================================

    // 组装上下文 — 一站式入口。
    //
    // 使用内部存储的 project_（由 setProject 设置）。
    //
    // 处理流程（共 4 步）：
    //   1. 构建系统提示词（项目上下文 + 工具使用指南）
    //   2. Token 预算分配
    //   3. 实时用量检查 + 自动压缩 + 告警（三级决策：Normal/Warning/Critical）
    //   4. 缓存警告/当前大小（供 Agent/REPL 在下一次请求前读取）
    //
    // llm_client 用于自动压缩（为 nullptr 时跳过自动压缩检查）。
    // conversation 非 const — 自动压缩可能删除旧消息并插入摘要。
    ContextAssembly assemble(
        llm::Conversation& conversation,
        int max_context_tokens,
        llm::ILLMClient* llm_client = nullptr);

    // 构建系统提示词（项目概要 + 工具使用指南）。
    // LLM 通过 get_latest_chapter / get_chapter_context 等工具按需获取章节上下文。
    std::string buildSystemPrompt(const Project& project);

    // ================================================================
    // Project 注入（供 assemble 使用）
    // ================================================================

    // 设置当前项目（非拥有指针，生命周期由 NovelAgentApp 管理）。
    void setProject(const Project* p) { project_ = p; }

    // ================================================================
    // Token 校准（自校准 TokenCounter）
    // ================================================================

    // 设置 Token 校准器（非拥有指针，生命周期由 NovelAgentApp 管理）。
    void setCalibrator(llm::TokenCounter* cal) { calibrator_ = cal; }
    // 设置当前使用的模型名（用于按模型区分校准数据）。
    void setModelName(const std::string& name) { model_name_ = name; }
    // 获取 Token 校准器（可能为 nullptr）。
    llm::TokenCounter* calibrator() { return calibrator_; }
    // 是否有可用的 Token 校准器。
    bool hasCalibrator() const { return calibrator_ != nullptr && !model_name_.empty(); }

    // ================================================================
    // 会话级 Token 追踪（委托 TokenTracker）
    // ================================================================

    // 设置模型上下文窗口上限（从 ProviderConfig 获取）。
    void setModelContextLimit(int limit) { tracker_.setModelLimit(limit); }

    // 累计一次请求的 token 消耗。
    void recordUsage(int input_tokens, int output_tokens) { tracker_.record(input_tokens, output_tokens); }

    // 请求前检查上下文用量状态（基于最近一次请求的实际大小）。
    PreRequestResult checkThresholds() const { return tracker_.check(); }
    // 基于传入的实时 token 数做用量检查（基于调用方刚算好的数据）。
    PreRequestResult checkThresholds(int realtime_total_tokens) const { return tracker_.check(realtime_total_tokens); }

    // 返回累计统计。
    SessionTokenState sessionStats() const { return tracker_.snapshot(); }

    // 返回当前用量百分比 [0, 100]。
    int usagePercent() const { return tracker_.usagePercent(); }

    // 重置会话统计（/clear 时调用）。
    void resetSession();

    // ================================================================
    // 对话压缩（LLM 驱动的摘要压缩）
    // ================================================================

    // 执行对话压缩 — 用 LLM 将旧消息摘要为一段文本。
    CompactResult compact(
        llm::Conversation& conversation,
        llm::ILLMClient& llm_client,
        std::optional<std::string> focus = std::nullopt);

    // 当前是否有压缩摘要。
    bool hasCompactedSummary() const { return !summary_.empty(); }
    // 清除压缩摘要及标记。
    void clearCompactedSummary() { summary_.clear(); marker_ = 0; }
    // 返回压缩标记位（被 compact() 压缩的消息数，0 = 无压缩）。
    int compactionMarker() const { return marker_; }

    // 设置自动 compaction（达到阈值自动触发）。
    void setAutoCompact(bool enabled, int threshold_pct = 95);
    // 是否应该触发自动压缩（usage_percent 直接传入，不依赖 tracker_ 陈旧数据）。
    bool shouldAutoCompact(int usage_percent) const;

    // 四级阈值配置（转发到 TokenTracker）。
    void setWarningThreshold(int pct)      { tracker_.setWarningThreshold(pct); }
    void setCriticalThreshold(int pct)     { tracker_.setCriticalThreshold(pct); }
    void setAutoCompactThreshold(int pct)  { tracker_.setAutoCompactThreshold(pct); }

    // 从持久化恢复压缩状态（供 loadSessionState 使用）。
    void restoreCompactionState(const std::string& summary, int new_marker) {
        summary_ = summary;
        marker_ = new_marker;
    }

    // 返回最后一次 assemble() 产生的警告列表。
    const std::vector<std::string>& lastWarnings() const { return last_warnings_; }

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
                          const std::vector<size_t>& preserved_indices);

    // 加载完整会话状态并恢复到 ContextManager 内部状态。
    // 自动对比 project_mtime，如果 Project 被修改过则清空压缩摘要。
    void loadSessionState(llm::Conversation& conv);

    // 公开子组件（供测试/诊断使用）
    TokenTracker& tracker() { return tracker_; }
    const TokenTracker& tracker() const { return tracker_; }

private:
    FileStorageBackend& storage_;
    SessionPersistence persistence_;

    TokenTracker tracker_;

    // ── Compaction 状态（原 Compactor 成员，已展开合并）──
    std::string summary_;               //  LLM 生成的压缩摘要
    int marker_ = 0;                    //  被压缩的消息数标记，/rewind 检测用
    bool auto_compact_ = false;         //  是否启用自动压缩

    // ── Project 注入（非拥有）──
    const Project* project_ = nullptr;

    // ── 会话级状态 ──
    std::vector<std::string> last_warnings_;  //  最后一次 assemble() 的警告缓存

    // ── Token 校准（自校准 TokenCounter）──
    llm::TokenCounter* calibrator_ = nullptr;  //  Token 校准器（非拥有，nullptr=降级）
    std::string model_name_;                      //  当前模型名（用于按模型区分校准）

    // ── 压缩实现细节 ──
    // 构建发送给压缩 LLM 的提示词（含对话文本和可选焦点）。
    std::string buildCompactPrompt(const std::string& conversation_text,
                                   const std::optional<std::string>& focus) const;
    // 估算文本的 token 数（带校准，校准器不可用时降级为纯估算）。
    int estimateTokens(const std::string& text) const;
};

} // namespace agent
