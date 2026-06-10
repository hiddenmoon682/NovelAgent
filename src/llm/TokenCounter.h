#pragma once

#include <string>
#include <vector>
#include "llm/Message.h"

namespace llm {

/// 启发式 Token 估算器，用于请求前的上下文窗口预算判断。
/// 所有结果均为近似值，实际 token 数以 API 返回的 usage 字段为准。
class TokenCounter {
public:
    /// 估算单段文本的 token 数（中文: 每字 0.75, 英文: 每词 1.3）
    static int countTokens(const std::string& text);

    /// 估算消息列表的总 token 数（含角色标记和工具调用的结构开销）
    static int countMessages(const std::vector<Message>& messages);

    /// 估算单条消息的 token 数（避免为截断循环构造临时 vector）
    static int countSingleMessage(const Message& msg);

    /// 统计文本中的中文字符数（CJK 统一表意文字）
    static int estimateChineseChars(const std::string& text);

    /// 统计文本中的英文单词数（连续拉丁字母序列）
    static int estimateEnglishWords(const std::string& text);

private:
    TokenCounter() = default;
};

} // namespace llm
