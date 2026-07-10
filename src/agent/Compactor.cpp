// Compactor 实现 — LLM 驱动的对话压缩。

#include "agent/Compactor.h"

#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"
#include "project/Models.h"
#include "agent/PromptContextBuilder.h"

#include <spdlog/spdlog.h>
#include <sstream>

namespace agent {

namespace {

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

// 构建项目设定参考文本，供 compaction 时附加到 LLM 请求中。
// 目的是让压缩器在生成摘要时能正确识别当前章节的角色、设定、世界规则等人称指代，
// 避免因缺少上下文而导致摘要中人物指代混乱或情节事实失真。
//
// 流程：
//   1. 通过 PromptContextBuilder 获取当前章节的项目上下文（角色/设定/规则等）
//   2. 将渲染后的提示文本截断到 1200 字节（保留最核心信息，避免挤占压缩空间）
//   3. 截断时按 UTF-8 字符边界对齐，不会切断多字节中文字符
//
// project    项目数据（可能为 nullptr，此时返回空串）
// chapter_id 当前章节 ID，用于提取相关上下文
// 截断后的项目设定参考文本，UTF-8 安全截断
std::string buildProjectRef(const Project* project, const std::string& chapter_id) {
    if (!project) return {};
    prompt::PromptContextOptions options;
    options.task = "write_chapter";
    options.chapter_id = chapter_id;
    auto ctx = prompt::PromptContextBuilder::buildForChapter(*project, options);
    if (!ctx) return {};
    std::string text = ctx->rendered_prompt;
    // 截断到 1200 字节，确保按 UTF-8 字符边界对齐
    if (text.size() > 1200) {
        size_t trunc_len = 1200;
        while (trunc_len > 0 && (static_cast<unsigned char>(text[trunc_len]) & 0xC0) == 0x80) {
            --trunc_len;
        }
        text = text.substr(0, trunc_len) + "...";
    }
    return text;
}

} // namespace

CompactResult Compactor::compact(
    llm::Conversation& conversation,
    llm::ILLMClient& llm_client,
    const std::string& chapter_id,
    const Project* project,
    std::optional<std::string> focus)
{
    CompactResult result;
    const auto& all_msgs = conversation.all();
    int total_msgs = static_cast<int>(all_msgs.size());

    // 计算保留边界：保留最后 kCompactKeepExchanges * 2 条消息
    int keep_count = kCompactKeepExchanges * 2;
    if (total_msgs <= keep_count) {
        spdlog::info("[Compactor] compact 跳过: 消息不足 ({} 条)", total_msgs);
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

    // MED-1: 估算总 token，超出模型窗口则降级截断，防止 API 400
    int convo_tokens = llm::TokenCounter::countTokens(conversation_text);
    int prompt_tokens = llm::TokenCounter::countTokens(compact_prompt);
    int total_estimated = convo_tokens + prompt_tokens + 50; // 50 预留消息结构开销
    if (model_context_limit_ > 0 && total_estimated > model_context_limit_) {
        spdlog::warn("[Compactor] 待压缩对话 {} tokens 超过模型窗口 {}，"
                     "截断后再提交", total_estimated, model_context_limit_);
        // 截断 conversation_text 到窗口的 70%（留 30% 给 prompt+摘要回复）
        size_t max_convo_chars = conversation_text.size()
            * model_context_limit_ / total_estimated * 7 / 10;
        if (max_convo_chars < conversation_text.size()) {
            max_convo_chars = (std::max)(max_convo_chars, size_t{500});
            conversation_text = conversation_text.substr(0, max_convo_chars)
                              + "\n...(已截断)";
        }
    }
    std::string ctx = buildProjectRef(project, chapter_id);
    if (!ctx.empty()) {
        compact_prompt += "\n\n当前项目设定参考（用于正确识别人物指代）：\n" + ctx;
    }
    if (focus && !focus->empty()) {
        compact_prompt += std::string("\n特别注意：") + *focus;
    }

    std::vector<llm::Message> compact_msgs = {
        llm::Message::user(conversation_text)
    };

    try {
        auto response = llm_client.chatNonStreaming(compact_msgs, {}, compact_prompt);
        result.summary = response.content;
        result.tokens_after = llm::TokenCounter::countTokens(result.summary);

        summary_ = result.summary;
        marker_ = compact_count;

        // 从 conversation 头部删除已压缩的旧消息
        conversation.removeOldest(compact_count);

        spdlog::info("[Compactor] compact 完成: {} 条 → 摘要 ({} → {} tokens, {:.0f}%)",
                     compact_count, result.tokens_before, result.tokens_after,
                     result.tokens_before > 0
                         ? (1.0 - static_cast<double>(result.tokens_after) / result.tokens_before) * 100.0
                         : 0.0);
    } catch (const std::exception& e) {
        spdlog::error("[Compactor] compact LLM 调用失败: {}", e.what());
        result.summary = "(压缩失败: " + std::string(e.what()) + ")";
        result.messages_compacted = 0;
    }

    return result;
}

} // namespace agent
