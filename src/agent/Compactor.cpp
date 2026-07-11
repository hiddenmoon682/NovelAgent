// Compactor 实现 — LLM 驱动的对话压缩。

#include "agent/Compactor.h"

#include "llm/ILLMClient.h"
#include "llm/TokenCalibrator.h"
#include "llm/TokenCounter.h"

#include <spdlog/spdlog.h>
#include <sstream>

namespace agent {

namespace {

// Compaction 时保留的最近消息对数（理想值）。
constexpr int kCompactKeepExchanges = 10;   // 保留最近 10 对 = ~20 条消息
// Compaction 的最小保留消息对数（防止压缩过度丢失全部上下文）。
constexpr int kMinKeepExchanges = 2;        // 最少保留 2 对 = 4 条消息

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
    "总长度控制在 2000 字以内，事实与风格的比例由你判断。\n"
    "\n"
    "注意：对话历史中可能包含之前生成的压缩摘要，\n"
    "请以已有摘要中的情节事实为基础，补充新增对话中的关键进展，\n"
    "避免过度概括或丢失已有摘要中的细节。";

} // namespace

CompactResult Compactor::compact(
    llm::Conversation& conversation,
    llm::ILLMClient& llm_client,
    std::optional<std::string> focus)
{
    CompactResult result;
    const auto& all_msgs = conversation.all();
    int total_msgs = static_cast<int>(all_msgs.size());

    // 计算保留边界：理想保留 kCompactKeepExchanges*2 条，但若消息不够则保留更少。
    // 不设"消息太少不压缩"的守卫——compact 被调用说明调用方已判断上下文承压
    //（token 用量高或截断已发生），此时即使只有少量消息也可能消耗大量 token
    //（每条消息可能是完整章节正文），应照常压缩。唯一硬约束：至少 1 条可压缩。
    const int ideal_keep = kCompactKeepExchanges * 2;   // 20
    const int min_keep = kMinKeepExchanges * 2;          // 4

    if (total_msgs <= 1) {
        spdlog::info("[Compactor] compact 跳过: 没有可压缩的消息 ({} 条)", total_msgs);
        result.summary = "(消息数量不足，无法压缩)";
        return result;
    }

    int keep_count = ideal_keep;
    if (total_msgs <= ideal_keep) {
        // 消息不够理想保留数 → 尽量少保留，多压缩
        keep_count = std::min(min_keep, total_msgs - 1);
        if (keep_count == 0) keep_count = 1;  // 至少保留最后一条
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
    // 应用 Token 校准修正因子（若已注入）
    if (calibrator_ && !model_name_.empty()) {
        convo_tokens = calibrator_->apply(model_name_, convo_tokens);
        prompt_tokens = calibrator_->apply(model_name_, prompt_tokens);
    }
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

    if (focus && !focus->empty()) {
        compact_prompt += std::string("\n特别注意：") + *focus;
    }

    std::vector<llm::Message> compact_msgs = {
        llm::Message::user(conversation_text)
    };

    try {
        // 计算当前实际发送的估算值（供校准回传使用）
        int send_convo = llm::TokenCounter::countTokens(conversation_text);
        int send_prompt = llm::TokenCounter::countTokens(compact_prompt);
        if (calibrator_ && !model_name_.empty()) {
            send_convo = calibrator_->apply(model_name_, send_convo);
            send_prompt = calibrator_->apply(model_name_, send_prompt);
        }
        int estimated_input = send_convo + send_prompt + 50;

        auto response = llm_client.chatNonStreaming(compact_msgs, {}, compact_prompt);
        result.summary = response.content;
        // 使用 API 返回的真实 token 数，不再用启发式估算
        result.tokens_after = response.completion_tokens;

        // 将 compaction LLM 调用的真实 token 数回传给校准器
        if (calibrator_ && !model_name_.empty() && estimated_input > 0 && response.prompt_tokens > 0) {
            calibrator_->calibrate(model_name_, estimated_input, response.prompt_tokens);
            spdlog::debug("[Compactor] Token 校准: model={}, estimated={}, actual={}",
                          model_name_, estimated_input, response.prompt_tokens);
        }

        summary_ = result.summary;
        marker_ = compact_count;

        // 从 conversation 头部删除已压缩的旧消息
        conversation.removeOldest(compact_count);

        // 将被压缩的对话摘要以 user/assistant 消息对插入对话头部，
        // 作为被删除的旧消息的语义替代。
        // 摘要作为对话消息而非追加到 system prompt，使得：
        //   1. 下次 compact() 能自然看到并重新压缩旧摘要
        //   2. system prompt 保持稳定，API 缓存可命中
        // 注意：不要使用 System 角色——放入 messages 中的 System 角色消息
        // 会导致 API system prompt 缓存失效，与放在 system prompt 中无异。
        conversation.addUser("【系统】以下是被压缩的旧对话摘要：");
        conversation.addAssistant("[被压缩的历史摘要]\n" + result.summary);

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
