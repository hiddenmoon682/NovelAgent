#pragma once

/// 对话摘要器 — 从对话历史中规则提取关键信息。
///
/// Phase 4 架构改进：从 ContextManager 拆分出独立职责。
/// 不调用 LLM，使用关键词 + 正则匹配做规则提取。
///
/// 线程安全：纯计算无副作用，所有方法为静态。

#include "agent/ContextManagerTypes.h"

#include <string>
#include <vector>

namespace llm {
enum class MessageRole;
struct Message;
} // namespace llm

namespace agent {

/// 配置化关键词 — 可针对不同语言/写作风格定制。
struct SummaryKeywords {
    std::vector<std::string> plot_keywords = {
        "剧情", "情节", "冲突", "转折", "高潮", "伏笔", "悬念",
        "发展", "推进", "变化", "揭示", "收束", "展开"
    };
    std::vector<std::string> task_keywords = {
        "写", "创作", "修改", "检查", "分析", "生成",
        "创建", "更新", "删除", "添加", "完成", "开始",
        "写一", "编撰", "续写", "改写"
    };
    int max_plot_points = 5;
    int max_tasks = 3;
};

/// 对话摘要器。
class ConversationSummarizer {
public:
    ConversationSummarizer() = default;

    /// 使用自定义关键词配置。
    explicit ConversationSummarizer(const SummaryKeywords& keywords)
        : keywords_(keywords) {}

    /// 从对话历史中规则提取关键信息。
    ConversationSummary summarize(
        const std::vector<llm::Message>& messages) const;

    /// 将结构化摘要渲染为可注入上下文的文本。
    static std::string render(const ConversationSummary& summary);

    /// 更新摘要关键词（运行时定制）。
    void setKeywords(const SummaryKeywords& kw) { keywords_ = kw; }
    const SummaryKeywords& keywords() const { return keywords_; }

private:
    SummaryKeywords keywords_;

    static std::vector<std::string> extractCharacterNames(
        const std::vector<llm::Message>& messages);
    static std::vector<std::string> extractChapterRefs(
        const std::vector<llm::Message>& messages);
    std::vector<std::string> extractPlotPoints(
        const std::vector<llm::Message>& messages) const;
    std::vector<std::string> extractTasks(
        const std::vector<llm::Message>& messages) const;
    static std::vector<std::string> splitSentences(const std::string& text);
};

} // namespace agent
