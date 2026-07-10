#pragma once

// Compactor — LLM 驱动的对话压缩器（Issue 3 拆分自 ContextManager）。
//
// 职责：将旧对话历史交由 LLM 压缩为双层摘要（情节事实 + 风格样本），
// 作为中期记忆层注入后续请求的 system prompt。
//
// 依赖：ILLMClient&（仅在 compact() 调用时使用）

#include "agent/ContextManagerTypes.h"
#include "llm/Conversation.h"

#include <optional>
#include <string>

namespace llm {
class ILLMClient;
} // namespace llm

struct Project;

namespace agent {

class Compactor {
public:
    Compactor() = default;

    // 执行 LLM 驱动的对话压缩。
    // @param conversation  当前对话历史（压缩成功后会从头部删除被压缩的消息）
    // @param llm_client    LLM 客户端（用于生成压缩摘要）
    // @param chapter_id    当前活跃章节 ID（用于注入项目设定参考）
    // @param project       项目指针（用于 buildSystemPrompt，可为 nullptr）
    // @param focus         可选压缩焦点
    // @returns 压缩结果（摘要文本 + token 变化 + 压缩消息数）
    CompactResult compact(
        llm::Conversation& conversation,
        llm::ILLMClient& llm_client,
        const std::string& chapter_id,
        const Project* project = nullptr,
        std::optional<std::string> focus = std::nullopt);

    // 当前是否有压缩摘要。
    bool hasSummary() const { return !summary_.empty(); }
    // 获取压缩摘要文本。
    const std::string& summary() const { return summary_; }
    // 清除压缩摘要及标记。
    void clear() { summary_.clear(); marker_ = 0; }
    // 返回压缩标记位（被 compact() 压缩的消息数，0 = 无压缩）。
    int marker() const { return marker_; }

    // Issue 3: 从持久化恢复压缩状态（供 ContextManager::loadSessionState 使用）。
    void restore(const std::string& summary, int new_marker) {
        summary_ = summary;
        marker_ = new_marker;
    }

    // 设置自动压缩参数。
    void setAutoCompact(bool enabled, int threshold_pct = 70) {
        auto_compact_ = enabled;
        if (threshold_pct > 0 && threshold_pct <= 100)
            auto_compact_threshold_ = threshold_pct;
    }
    // 是否应该触发自动压缩（由 ContextManager 协调 TokenTracker 用量）。
    bool shouldAutoCompact(int usage_percent) const {
        if (!auto_compact_) return false;
        return usage_percent >= auto_compact_threshold_;
    }
    // 压缩后向量索引位置失效，由 ContextManager 调用此方法清除脏标记。
    void clearVectorDirtyFlag(bool& dirty) const { dirty = false; }

    // MED-1: 设置模型上下文窗口上限，compact 前做 token 预算检查防 API 400。
    void setModelContextLimit(int limit) { model_context_limit_ = limit; }

private:
    std::string summary_;            //< LLM 生成的压缩摘要
    int marker_ = 0;                 //< 被压缩的消息数标记，/rewind 检测用
    bool auto_compact_ = false;      //< 是否启用自动压缩
    int auto_compact_threshold_ = 70; //< 自动压缩触发阈值（用量百分比）
    int model_context_limit_ = 0;    //< MED-1: 模型上下文窗口上限（0=不限制）
};

} // namespace agent
