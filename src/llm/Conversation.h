#pragma once

#include "llm/Message.h"
#include <string>
#include <vector>

namespace llm {

/// 对话历史管理器 — 封装消息列表的便捷操作。
///
/// 职责：
/// - 维护有序消息列表（system → user → assistant → tool → ...）
/// - 提供按角色添加消息的便捷方法
/// - 分离 system 消息与其他消息（与 LLMClient::chat() 参数语义对齐）
///
/// 使用示例：
///   Conversation conv;
///   conv.addSystem("你是一个有用的助手。");
///   conv.addUser("你好");
///   conv.addAssistant("你好！有什么可以帮助你的？");
///
///   // 传给 LLMClient
///   client.chat(conv.messages(), {}, conv.systemPrompt());
///
/// 注意：Conversation 不负责持久化——序列化由 Agent 层处理。
class Conversation {
public:
    // ================================================================
    // 添加消息
    // ================================================================

    /// 添加一条已构造好的消息（通用接口）
    Conversation& add(Message msg) {
        messages_.push_back(std::move(msg));
        return *this;
    }

    /// 添加用户消息
    Conversation& addUser(std::string content) {
        return add(Message::user(std::move(content)));
    }

    /// 添加系统提示词消息（通常只有一条，放在最前面）
    Conversation& addSystem(std::string content) {
        return add(Message::system(std::move(content)));
    }

    /// 添加 AI 助手消息
    Conversation& addAssistant(std::string content) {
        return add(Message::assistant(std::move(content)));
    }

    /// 添加工具调用结果消息
    Conversation& addToolResult(std::string call_id, std::string content) {
        return add(Message::toolResult(std::move(call_id), std::move(content)));
    }

    // ================================================================
    // 查询
    // ================================================================

    /// 提取系统提示词（首条 system 角色消息的 content）。
    /// 如果没有 system 消息，返回空字符串。
    std::string systemPrompt() const {
        for (const auto& msg : messages_) {
            if (msg.role == MessageRole::System) {
                return msg.content;
            }
        }
        return {};
    }

    /// 获取不含 system 消息的对话历史（传给 LLMClient::chat）。
    /// system 消息由 LLMClient::chat() 的 system_prompt 参数单独传递。
    std::vector<Message> messages() const {
        std::vector<Message> result;
        result.reserve(messages_.size());
        for (const auto& msg : messages_) {
            if (msg.role != MessageRole::System) {
                result.push_back(msg);
            }
        }
        return result;
    }

    /// 获取所有消息（含 system 消息），用于调试和完整遍历。
    const std::vector<Message>& all() const { return messages_; }

    // ================================================================
    // 容器操作
    // ================================================================

    bool empty() const { return messages_.empty(); }
    size_t size() const { return messages_.size(); }

    Message& back() { return messages_.back(); }
    const Message& back() const { return messages_.back(); }

    Message& front() { return messages_.front(); }
    const Message& front() const { return messages_.front(); }

    Message& operator[](size_t i) { return messages_[i]; }
    const Message& operator[](size_t i) const { return messages_[i]; }

    void clear() { messages_.clear(); }
    void popBack() { messages_.pop_back(); }

    void reserve(size_t n) { messages_.reserve(n); }

    // ================================================================
    // 迭代器
    // ================================================================

    auto begin()       { return messages_.begin(); }
    auto end()         { return messages_.end(); }
    auto begin() const { return messages_.begin(); }
    auto end()   const { return messages_.end(); }

private:
    std::vector<Message> messages_;
};

} // namespace llm
