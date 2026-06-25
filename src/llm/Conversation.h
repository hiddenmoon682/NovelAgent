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
    // 容器操作（只读，修改通过 add*() 方法保证一致性）
    // ================================================================

    bool empty() const { return messages_.empty(); }
    size_t size() const { return messages_.size(); }

    const Message& back() const { return messages_.back(); }
    const Message& front() const { return messages_.front(); }
    const Message& operator[](size_t i) const { return messages_[i]; }

    void clear() { messages_.clear(); }
    void popBack() { messages_.pop_back(); }
    void reserve(size_t n) { messages_.reserve(n); }

    /// 截断到前 N 条消息（保留 [0, keep_count)），丢弃其余。
    void truncateTo(size_t keep_count) {
        if (keep_count < messages_.size()) messages_.resize(keep_count);
    }

    /// 编辑指定索引的消息内容（仅允许 User 和 Assistant 消息）。
    /// 返回 false 表示索引越界或角色不允许编辑。
    bool editMessage(size_t index, std::string new_content) {
        if (index >= messages_.size()) return false;
        auto& msg = messages_[index];
        if (msg.role != MessageRole::User && msg.role != MessageRole::Assistant)
            return false;
        msg.content = std::move(new_content);
        return true;
    }

    // ── 消息保留标记（Pin）──
    /// 按 all() 索引标记消息为"保留"，截断时优先保留。
    /// 返回 false 表示索引越界。
    bool pinMessage(size_t index) {
        if (index >= messages_.size()) return false;
        messages_[index].preserved = true;
        return true;
    }
    /// 取消保留标记。
    bool unpinMessage(size_t index) {
        if (index >= messages_.size()) return false;
        messages_[index].preserved = false;
        return true;
    }
    /// 获取所有保留消息的索引（按 all() 顺序）。
    std::vector<size_t> pinnedIndices() const {
        std::vector<size_t> result;
        for (size_t i = 0; i < messages_.size(); ++i) {
            if (messages_[i].preserved) result.push_back(i);
        }
        return result;
    }

    // ================================================================
    // 迭代器（只读）
    // ================================================================

    auto begin() const { return messages_.begin(); }
    auto end()   const { return messages_.end(); }

private:
    std::vector<Message> messages_;
};

} // namespace llm
