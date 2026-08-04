#pragma once

#include "llm/Message.h"

#include <optional>
#include <string>
#include <vector>

namespace llm {
class ILLMClient;
}

namespace agent {

// 压缩结果——携带摘要、被压缩消息原文、保留消息列表和压缩前后的 token 统计。
struct CompactionResult {
    std::string summary;                      // LLM 生成的对话摘要文本
    std::vector<llm::Message> compacted;      // 被压缩掉的旧消息原文（供完整历史层归档）
    std::vector<llm::Message> retained;       // 摘要对 + 保留的近期消息
    int tokens_before = 0;                    // 压缩前被压缩部分的 token 数
    int tokens_after = 0;                     // 压缩后摘要的 token 数（≈ completion_tokens）
    int messages_compacted = 0;               // 实际被压缩的消息条数
};

// 无状态压缩策略。输入消息列表 → 输出摘要 + 保留的近期消息。
//
// 职责：
//   - 将对话历史中较旧的部分发给 LLM 生成中文双层摘要（情节事实 + 风格参考）；
//   - 保留最近 N 对消息不压缩，维持短期上下文；
//   - 不修改任何外部状态，由调用方（如 Memory）决定何时将摘要写入持久化。
//
// 使用方式：
//   Compactor compactor({ .keep_exchanges = 5, .min_keep = 2 });
//   auto result = compactor.compact(messages, llm_client, "当前场景");
class Compactor {
public:
    // 压缩策略配置
    struct Config {
        int keep_exchanges = 5;   // 保留最近 N 轮（用户+助手=1 轮）消息不压缩
        int min_keep = 2;         // 最少保留的轮数——即使总消息数很少也不低于此值
    };

    Compactor() = default;
    explicit Compactor(Config config) : config_(config) {}

    // 执行一次压缩。
    // messages:  完整消息列表（排序从旧到新）
    // llm:       LLM 客户端，用于生成摘要
    // focus:     可选聚焦方向（如指定场景/角色）
    // 返回:      包含摘要、保留消息和压缩统计
    CompactionResult compact(const std::vector<llm::Message>& messages,
                             llm::ILLMClient& llm,
                             std::optional<std::string> focus = std::nullopt) const;

private:
    Config config_;  // 保留轮数等策略参数

    // 将消息列表格式化为纯文本（角色标签 + 内容），供 LLM 压缩
    static std::string buildConversationText(const std::vector<llm::Message>& msgs);

    // 构建发给 LLM 的 system prompt（kCompactSystemPrompt + 可选的 focus）
    static std::string buildCompactPrompt(const std::optional<std::string>& focus);
};

} // namespace agent
