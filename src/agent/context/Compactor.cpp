// ============================================================
// Compactor.cpp — 对话历史压缩器实现
//
// 功能：将较旧的对话消息提交给 LLM 生成双层摘要（情节事实 + 风格参考），
//       保留近期 N 轮消息维持短期上下文，从而控制对话历史的 token 消耗。
// ============================================================

#include "agent/context/Compactor.h"

#include "agent/prompt/Prompts.h"
#include "llm/ILLMClient.h"
#include "llm/TokenCounter.h"

#include <spdlog/spdlog.h>
#include <sstream>

namespace agent {

// ------------------------------------------------------------------
// buildConversationText — 将 Message 列表渲染为纯文本
//
// 每条消息格式： [角色] 内容
// 对 Assistant 消息额外附带 [思考过程] 行（reasoning_content）。
// ------------------------------------------------------------------
std::string Compactor::buildConversationText(const std::vector<llm::Message>& msgs) {
    std::ostringstream oss;
    for (const auto& msg : msgs) {
        // 将 MessageRole 枚举映射为中文角色标签
        std::string role_str;
        switch (msg.role) {
            case llm::MessageRole::User:       role_str = "用户"; break;
            case llm::MessageRole::Assistant:  role_str = "助手"; break;
            case llm::MessageRole::Tool:       role_str = "工具"; break;
            default: continue;  // 跳过 System 等不需要压缩的角色
        }
        oss << "[" << role_str << "] " << msg.content << "\n";

        // 助手消息的思考过程单独成行，方便 LLM 理解推理脉络
        if (msg.role == llm::MessageRole::Assistant && !msg.reasoning_content.empty()) {
            oss << "  [思考过程] " << msg.reasoning_content << "\n";
        }
    }
    return oss.str();
}

// ------------------------------------------------------------------
// buildCompactPrompt — 组装最终发给 LLM 的 system prompt
//
// 以 kCompactSystemPrompt 为基础，若调用方传入了 focus 参数，则
// 追加一行"特别注意："指引 LLM 的压缩方向。
// ------------------------------------------------------------------
std::string Compactor::buildCompactPrompt(const std::optional<std::string>& focus) {
    std::string prompt = prompt::kCompactSystemPrompt;
    if (focus && !focus->empty()) {
        prompt += std::string("\n特别注意：") + *focus;
    }
    return prompt;
}

// ------------------------------------------------------------------
// compact — 压缩主流程
//
// 步骤：
//   1. 根据 Config 计算需要保留的近期消息数（按轮数×2 取消息数），
//      剩余的划为"待压缩区"；
//   2. 将待压缩区渲染为纯文本，发给 LLM 生成摘要；
//   3. 构建返回结果：user 占位消息 + assistant 摘要 → 后接保留的近期消息；
//   4. 若 LLM 调用失败，回退为原消息列表，不执行压缩。
//
// 注意：这里只做统计和摘要生成，实际持久化由 Memory 层负责。
// ------------------------------------------------------------------
CompactionResult Compactor::compact(
    const std::vector<llm::Message>& messages,
    llm::ILLMClient& llm,
    std::optional<std::string> focus) const
{
    CompactionResult result;
    int total_msgs = static_cast<int>(messages.size());

    // 轮数 → 消息数：每轮含 1 条 user + 1 条 assistant
    const int ideal_keep = config_.keep_exchanges * 2;
    const int min_keep = config_.min_keep * 2;

    // 消息太少时不做压缩
    if (total_msgs <= 1) {
        result.summary = "(消息数量不足，无法压缩)";
        result.retained = messages;
        return result;
    }

    // 计算保留条数：优先理想值，但总消息很少时用 min_keep 兜底
    int keep_count = ideal_keep;
    if (total_msgs <= ideal_keep) {
        keep_count = std::min(min_keep, total_msgs - 1);
        if (keep_count == 0) keep_count = 1;
    }
    int compact_count = total_msgs - keep_count;

    // WHY：保留窗口按“最近 N 条”切割，切割点可能落在 assistant(tool_calls)
    // 与其 tool 结果之间——保留区将以孤儿 tool 消息开头（tool_call_id 无配对），
    // 不符合 OpenAI 协议。向前回退切割点，直到保留区首条不是 tool 消息，
    // 使 tool 结果与其 assistant 消息始终留在同一侧，保证工具循环中途
    // 触发压缩后的消息序列仍然合法。
    while (compact_count > 0 &&
           messages[compact_count].role == llm::MessageRole::Tool) {
        --compact_count;
    }
    if (compact_count <= 0) {
        result.summary = "(切割点对齐后无可压缩消息)";
        result.retained = messages;
        return result;
    }
    result.messages_compacted = compact_count;

    // 切割：前 compact_count 条送去压缩，剩余保留明文中
    std::vector<llm::Message> to_compact(messages.begin(), messages.begin() + compact_count);
    std::vector<llm::Message> recent(messages.begin() + compact_count, messages.end());

    // 统计压缩前的 token 数，供调用方做预算决策
    result.tokens_before = llm::TokenCounter::countMessages(to_compact);

    // 构造 LLM 调用所需的对话文本和 system prompt
    std::string conversation_text = buildConversationText(to_compact);
    std::string compact_prompt = buildCompactPrompt(focus);

    // 压缩请求只包含一条 user 消息（整段对话文本），由 system prompt 指导摘要方向
    std::vector<llm::Message> compact_msgs = {
        llm::Message::user(conversation_text)
    };

    try {
        // 调用 LLM（非流式）生成摘要
        auto response = llm.chatNonStreaming(compact_msgs, {}, compact_prompt);
        result.summary = response.content;
        result.tokens_after = response.completion_tokens;

        // 构建保留的消息列表：两条占位消息（user 提示 + assistant 摘要）→ 近期消息
        result.retained.reserve(recent.size() + 2);
        result.retained.push_back(
            llm::Message::user("【系统】以下是被压缩的旧对话摘要："));
        result.retained.push_back(
            llm::Message::assistant("[被压缩的历史摘要]\n" + result.summary));
        result.retained.insert(result.retained.end(), recent.begin(), recent.end());

        spdlog::info("[Compactor] {} 条 → 摘要 ({} → {} tokens)",
                     compact_count, result.tokens_before, result.tokens_after);
    } catch (const std::exception& e) {
        // 压缩失败时：不丢失任何消息，原样保留，外部可依据 summary 中的错误信息决策
        spdlog::error("[Compactor] LLM 调用失败: {}", e.what());
        result.summary = "(压缩失败: " + std::string(e.what()) + ")";
        result.messages_compacted = 0;
        result.retained = messages;
    }

    return result;
}

} // namespace agent
