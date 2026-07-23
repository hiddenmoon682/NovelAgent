#pragma once

#include "llm/Message.h"

#include <optional>
#include <string>
#include <vector>

namespace llm {
class ILLMClient;
}

namespace agent {

struct CompactionResult {
    std::string summary;
    std::vector<llm::Message> retained;
    int tokens_before = 0;
    int tokens_after = 0;
    int messages_compacted = 0;
};

// 无状态压缩策略。输入消息列表 → 输出摘要 + 保留的近期消息。
// 不修改任何外部状态，由调用方决定何时 apply 到 Memory。
class Compactor {
public:
    struct Config {
        int keep_exchanges = 5;   // 保留最近 N 对消息
        int min_keep = 2;         // 最少保留对数
    };

    Compactor() = default;
    explicit Compactor(Config config) : config_(config) {}

    CompactionResult compact(const std::vector<llm::Message>& messages,
                             llm::ILLMClient& llm,
                             std::optional<std::string> focus = std::nullopt) const;

private:
    Config config_;

    static std::string buildConversationText(const std::vector<llm::Message>& msgs);
    static std::string buildCompactPrompt(const std::optional<std::string>& focus);
};

} // namespace agent
