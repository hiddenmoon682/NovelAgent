#pragma once

#include "llm/Message.h"
#include <string>
#include <vector>

namespace llm {

// Issue 2: 对话修改的批量描述 — 将多次 add/pin/truncate 合并为一次原子操作。
// 生产者（ToolPipeline/SubAgent）返回 diff，消费者（Conversation）统一 apply。
struct ConversationDiff {
    std::vector<Message> added;          //  按顺序追加的消息
    std::vector<size_t> pinned_indices;  //  需 pin 的消息在 diff.added 中的索引，非全局索引。apply() 自动加 base_offset
    bool retryable = false;              //  错误是否可重试（由 ToolPipeline 设置）
};

// 对话历史管理器 — 封装消息列表的便捷操作。
//
// System 消息独立存储为 system_prompt_（只有一条），其余角色（User/Assistant/Tool）
// 存储在 messages_ 中，因此 messages() 直接返回内部引用无需拷贝过滤。
//
// 使用示例：
//   Conversation conv;
//   conv.setSystemPrompt("你是一个有用的助手。");
//   conv.addUser("你好");
//   conv.addAssistant("你好！有什么可以帮助你的？");
//
//   // 传给 LLMClient（messages() 零拷贝）
//   client.chat(conv.messages(), {}, conv.systemPrompt());
//
// 注意：Conversation 不负责持久化——序列化由 Agent 层处理。
class Conversation {
public:
    // ================================================================
    // 添加消息
    // ================================================================

    // 添加一条已构造好的消息（通用接口，追加到尾部）
    // System 角色消息存入 system_prompt_，其余存入 messages_。
    Conversation& add(Message msg) {
        if (msg.role == MessageRole::System) {
            system_prompt_ = std::move(msg.content);
        } else {
            messages_.push_back(std::move(msg));
        }
        return *this;
    }

    // 在对话头部插入一条消息（通用接口）
    // System 角色消息覆盖 system_prompt_，其余插入 messages_ 头部。
    Conversation& prepend(Message msg) {
        if (msg.role == MessageRole::System) {
            system_prompt_ = std::move(msg.content);
        } else {
            messages_.insert(messages_.begin(), std::move(msg));
        }
        return *this;
    }

    // 添加用户消息
    Conversation& addUser(std::string content) {
        return add(Message::user(std::move(content)));
    }

    // 设置系统提示词（替换旧的 system_prompt_）。
    Conversation& setSystemPrompt(std::string content) {
        system_prompt_ = std::move(content);
        return *this;
    }
    // 等价于 setSystemPrompt，保持向后兼容。
    Conversation& addSystem(std::string content) {
        return setSystemPrompt(std::move(content));
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

    // 返回系统提示词。
    // System prompt 的唯一来源。Agent::buildEffectivePrompt() 通过 PromptComposer 和
    // ContextManager 动态注入上下文后，通过 LLMClient::chat() 的 system_prompt 参数传递。
    const std::string& systemPrompt() const { return system_prompt_; }

    // 获取不含 system 消息的对话历史（传给 LLMClient::chat）。
    // 零拷贝 — 直接返回内部引用，system_prompt_ 通过独立的 systemPrompt() 获取。
    const std::vector<Message>& messages() const { return messages_; }

    // 获取全部消息（含 system 消息），用于持久化和调试。
    // 注意：返回值为临时 vector（拼接 system_prompt_ + messages_），仅在低频路径使用。
    std::vector<Message> all() const {
        if (system_prompt_.empty()) return messages_;
        std::vector<Message> result;
        result.reserve(1 + messages_.size());
        result.push_back(Message::system(system_prompt_));
        result.insert(result.end(), messages_.begin(), messages_.end());
        return result;
    }

    // ================================================================
    // 容器操作（只读，修改通过 add*() 方法保证一致性）
    // ================================================================

    bool empty() const { return messages_.empty() && system_prompt_.empty(); }
    size_t size() const { return messages_.size() + (system_prompt_.empty() ? 0 : 1); }

    const Message& back() const { return messages_.back(); }
    const Message& front() const { return messages_.front(); }
    // 按 all() 视角索引访问消息（0 = system_prompt_ 若存在，1+ = messages_）。
    // 返回 by value（system_prompt_ 是 std::string 而非 Message 成员）。
    Message operator[](size_t i) const {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (i < offset) return Message::system(system_prompt_);
        return messages_[i - offset];
    }

    void clear() {
        messages_.clear();
        system_prompt_.clear();
    }
    void popBack() { messages_.pop_back(); }
    void reserve(size_t n) { messages_.reserve(n); }

    // 截断到前 N 条消息（保留 [0, keep_count)），丢弃其余。
    // keep_count 计数包含 system_prompt_（如果存在）。
    void truncateTo(size_t keep_count) {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (keep_count == 0) {
            messages_.clear();
            system_prompt_.clear();
            return;
        }
        if (keep_count <= offset) {
            // 仅保留 system_prompt_
            messages_.clear();
            return;
        }
        size_t total = offset + messages_.size();
        if (keep_count >= total) return;  // No-op
        messages_.resize(keep_count - offset);
    }

    // 从头部删除 |count| 条非 system 消息（最旧的），保留 [count, size())。
    // system_prompt_ 不受影响，始终在索引 0 位置。
    void removeOldest(size_t count) {
        if (count >= messages_.size()) {
            messages_.clear();
        } else {
            messages_.erase(messages_.begin(),
                            messages_.begin() + static_cast<ptrdiff_t>(count));
        }
    }

    // 编辑指定索引的消息内容（仅允许 User 和 Assistant 消息）。
    // index 为 all() 视角的索引（0 = system_prompt_，1+ = messages_）。
    bool editMessage(size_t index, std::string new_content) {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;  // 不能编辑 system 消息
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        auto& msg = messages_[msg_idx];
        if (msg.role != MessageRole::User && msg.role != MessageRole::Assistant)
            return false;
        msg.content = std::move(new_content);
        msg.preserved = false;  // 编辑后重置保留标记
        return true;
    }

    // ── 消息保留标记（Pin）──
    // 以下 index 均为 all() 视角的索引。
    bool pinMessage(size_t index) {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;  // 不能 pin system 消息
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        messages_[msg_idx].preserved = true;
        return true;
    }
    bool unpinMessage(size_t index) {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        messages_[msg_idx].preserved = false;
        return true;
    }
    // 获取所有保留消息的索引（all() 视角）。
    std::vector<size_t> pinnedIndices() const {
        std::vector<size_t> result;
        size_t offset = system_prompt_.empty() ? 0 : 1;
        for (size_t i = 0; i < messages_.size(); ++i) {
            if (messages_[i].preserved) result.push_back(offset + i);
        }
        return result;
    }

    // 清空所有 Assistant 消息中的 reasoning_content（思考过程）。
    // 在 ToolCallLoop 结束后调用，释放上下文空间。
    void stripReasoningContent() {
        for (auto& msg : messages_) {
            if (msg.role == MessageRole::Assistant) {
                msg.reasoning_content.clear();
            }
        }
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
    // 迭代器（只读，遍历 messages_ 部分）
    // ================================================================

    auto begin() const { return messages_.begin(); }
    auto end()   const { return messages_.end(); }

private:
    std::vector<Message> messages_;   // 非 System 消息（User / Assistant / Tool）
    std::string system_prompt_;       // 唯一的一条 System 消息
};

} // namespace llm
