#include "agent/context/Compactor.h"

#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"

#include <spdlog/spdlog.h>
#include <sstream>

namespace agent {

namespace {

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

} // namespace

std::string Compactor::buildConversationText(const std::vector<llm::Message>& msgs) {
    std::ostringstream oss;
    for (const auto& msg : msgs) {
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
    return oss.str();
}

std::string Compactor::buildCompactPrompt(const std::optional<std::string>& focus) {
    std::string prompt = kCompactSystemPrompt;
    if (focus && !focus->empty()) {
        prompt += std::string("\n特别注意：") + *focus;
    }
    return prompt;
}

CompactionResult Compactor::compact(
    const std::vector<llm::Message>& messages,
    llm::ILLMClient& llm,
    std::optional<std::string> focus) const
{
    CompactionResult result;
    int total_msgs = static_cast<int>(messages.size());

    const int ideal_keep = config_.keep_exchanges * 2;
    const int min_keep = config_.min_keep * 2;

    if (total_msgs <= 1) {
        result.summary = "(消息数量不足，无法压缩)";
        result.retained = messages;
        return result;
    }

    int keep_count = ideal_keep;
    if (total_msgs <= ideal_keep) {
        keep_count = std::min(min_keep, total_msgs - 1);
        if (keep_count == 0) keep_count = 1;
    }
    int compact_count = total_msgs - keep_count;
    result.messages_compacted = compact_count;

    std::vector<llm::Message> to_compact(messages.begin(), messages.begin() + compact_count);
    std::vector<llm::Message> recent(messages.begin() + compact_count, messages.end());

    result.tokens_before = llm::TokenCounter::countMessages(to_compact);

    std::string conversation_text = buildConversationText(to_compact);
    std::string compact_prompt = buildCompactPrompt(focus);

    std::vector<llm::Message> compact_msgs = {
        llm::Message::user(conversation_text)
    };

    try {
        auto response = llm.chatNonStreaming(compact_msgs, {}, compact_prompt);
        result.summary = response.content;
        result.tokens_after = response.completion_tokens;

        // 构建保留的消息列表：摘要对 + 近期消息
        result.retained.reserve(recent.size() + 2);
        result.retained.push_back(
            llm::Message::user("【系统】以下是被压缩的旧对话摘要："));
        result.retained.push_back(
            llm::Message::assistant("[被压缩的历史摘要]\n" + result.summary));
        result.retained.insert(result.retained.end(), recent.begin(), recent.end());

        spdlog::info("[Compactor] {} 条 → 摘要 ({} → {} tokens)",
                     compact_count, result.tokens_before, result.tokens_after);
    } catch (const std::exception& e) {
        spdlog::error("[Compactor] LLM 调用失败: {}", e.what());
        result.summary = "(压缩失败: " + std::string(e.what()) + ")";
        result.messages_compacted = 0;
        result.retained = messages;
    }

    return result;
}

} // namespace agent
