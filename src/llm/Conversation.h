#pragma once

#include "llm/Message.h"
#include <string>
#include <vector>

namespace llm {

// Issue 2: 对话修改的批量描述 — 将多次 add/pin/truncate 合并为一次原子操作。
// 生产者（ToolPipeline/SubAgent）返回 diff，消费者（Conversation）统一 apply。
struct ConversationDiff {
    std::vector<Message> added;          //  按顺序追加的消息
    std::vector<size_t> pinned_indices;  //  需 pin 的消息索引（相对于 apply 前状态）
    bool retryable = false;              //  错误是否可重试（由 ToolPipeline 设置）
};

// 对话历史管理器 — 封装消息列表的便捷操作。
//
// 职责：
// - 维护有序消息列表（system → user → assistant → tool → ...）
// - 提供按角色添加消息的便捷方法
// - 分离 system 消息与其他消息（与 LLMClient::chat() 参数语义对齐）
//
// 使用示例：
//   Conversation conv;
//   conv.addSystem("你是一个有用的助手。");
//   conv.addUser("你好");
//   conv.addAssistant("你好！有什么可以帮助你的？");
//
//   // 传给 LLMClient
//   client.chat(conv.messages(), {}, conv.systemPrompt());
//
// 注意：Conversation 不负责持久化——序列化由 Agent 层处理。
class Conversation {
public:
    // ================================================================
    // 添加消息
    // ================================================================

    // 添加一条已构造好的消息（通用接口）
    Conversation& add(Message msg) {
        messages_.push_back(std::move(msg));
        return *this;
    }

    // 添加用户消息
    Conversation& addUser(std::string content) {
        return add(Message::user(std::move(content)));
    }

    // 添加系统提示词消息（通常只有一条，放在最前面）
    Conversation& addSystem(std::string content) {
        return add(Message::system(std::move(content)));
    }

    // 添加 AI 助手消息
    Conversation& addAssistant(std::string content) {
        return add(Message::assistant(std::move(content)));
    }

    // 添加工具调用结果消息
    Conversation& addToolResult(std::string call_id, std::string content) {
        return add(Message::toolResult(std::move(call_id), std::move(content)));
    }

    // ================================================================
    // 查询
    // ================================================================

    // 提取系统提示词（首条 system 角色消息的 content）。
    // 如果没有 system 消息，返回空字符串。
    //
    // ⚠️ 注意：此方法仅用于调试/日志查询，不作为 LLM 请求的 system prompt 来源。
    // System prompt 的实际来源是 Agent::system_prompt_（经过 PromptComposer 和
    // ContextManager 动态注入），通过 LLMClient::chat() 的 system_prompt 参数单独传递。
    // Conversation 中的 System 消息不会被发送给 LLM（见 messages() — 过滤了 System 角色）。
    std::string systemPrompt() const {
        for (const auto& msg : messages_) {
            if (msg.role == MessageRole::System) {
                return msg.content;
            }
        }
        return {};
    }

    // 获取不含 system 消息的对话历史（传给 LLMClient::chat）。
    // system 消息由 LLMClient::chat() 的 system_prompt 参数单独传递。
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

    // 获取所有消息（含 system 消息），用于调试和完整遍历。
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

    // 截断到前 N 条消息（保留 [0, keep_count)），丢弃其余。
    // 注意：被截断的消息中若有 preserved 标记的消息将被静默丢弃，
    // 调用方（如 Agent::rewindTo）应在截断前检查并提示用户。
    void truncateTo(size_t keep_count) {
        if (keep_count < messages_.size()) messages_.resize(keep_count);
    }

    // 从头部删除 |count| 条消息（最旧的），保留 [count, size())。
    // compact() 在生成摘要后用此方法删除已压缩的旧消息。
    void removeOldest(size_t count) {
        if (count >= messages_.size()) {
            messages_.clear();
        } else {
            messages_.erase(messages_.begin(),
                            messages_.begin() + static_cast<ptrdiff_t>(count));
        }
    }

    // 编辑指定索引的消息内容（仅允许 User 和 Assistant 消息）。
    // 返回 false 表示索引越界或角色不允许编辑。
    // Issue 27: 编辑后自动清除 preserved 标记（编辑视为"修改内容"而非"标记重要"，
    // 用户如需保留需重新 /pin）。
    bool editMessage(size_t index, std::string new_content) {
        if (index >= messages_.size()) return false;
        auto& msg = messages_[index];
        if (msg.role != MessageRole::User && msg.role != MessageRole::Assistant)
            return false;
        msg.content = std::move(new_content);
        msg.preserved = false;  // 编辑后重置保留标记
        return true;
    }

    // ── 消息保留标记（Pin）──
    // 按 all() 索引标记消息为"保留"，截断时优先保留。
    // 返回 false 表示索引越界。
    bool pinMessage(size_t index) {
        if (index >= messages_.size()) return false;
        messages_[index].preserved = true;
        return true;
    }
    // 取消保留标记。
    bool unpinMessage(size_t index) {
        if (index >= messages_.size()) return false;
        messages_[index].preserved = false;
        return true;
    }
    // 获取所有保留消息的索引（按 all() 顺序）。
    std::vector<size_t> pinnedIndices() const {
        std::vector<size_t> result;
        for (size_t i = 0; i < messages_.size(); ++i) {
            if (messages_[i].preserved) result.push_back(i);
        }
        return result;
    }

    // ── Issue 2: 批量修改（原子操作）──
    // 应用一组对话修改。add/addToolResult 之外的所有修改通过此方法集中执行。
    Conversation& apply(const ConversationDiff& diff) {
        size_t base_index = messages_.size();
        for (auto& m : diff.added) {
            messages_.push_back(std::move(m));
        }
        for (auto idx : diff.pinned_indices) {
            size_t abs_idx = base_index + idx;
            if (abs_idx < messages_.size())
                messages_[abs_idx].preserved = true;
        }
        return *this;
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
