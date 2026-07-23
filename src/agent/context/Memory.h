#pragma once

// Memory — 记忆状态管理器（原 Conversation），实现 IMemory 接口。
//
// System 消息独立存储为 system_prompt_（只有一条），其余角色（User/Assistant/Tool）
// 存储在 messages_ 中，因此 messages() 直接返回内部引用无需拷贝过滤。
//
// 使用示例：
//   Memory mem;
//   mem.injectSystemPrompt("你是一个有用的助手。");
//   mem.addUser("你好");
//   mem.addAssistant("你好！有什么可以帮助你的？");
//
//   // 传给 LLMClient（messages() 零拷贝）
//   client.chat(mem.messages(), {}, mem.systemPrompt());
//
// 注意：Memory 不负责持久化——序列化由 Agent 层处理。

#include "agent/context/IMemory.h"
#include "llm/Message.h"

#include <string>
#include <vector>

namespace llm {

class Memory : public IMemory {
public:
    // ================================================================
    // 注入（IMemory 核心接口实现）
    // ================================================================

    void inject(Message msg) override {
        if (msg.role == MessageRole::System) {
            system_prompt_ = std::move(msg.content);
        } else {
            messages_.push_back(std::move(msg));
        }
    }

    void injectSystemPrompt(std::string prompt) override {
        system_prompt_ = std::move(prompt);
    }

    void prepend(Message msg) override {
        if (msg.role == MessageRole::System) {
            system_prompt_ = std::move(msg.content);
        } else {
            messages_.insert(messages_.begin(), std::move(msg));
        }
    }

    // 批量原子修改（Issue 2: copy-then-swap 强异常保证）
    void apply(MemoryDiff diff) override {
        auto replacement = messages_;
        replacement.reserve(messages_.size() + diff.added.size());

        for (auto& m : diff.added) {
            replacement.push_back(std::move(m));
        }

        size_t base_index = messages_.size();
        for (auto idx : diff.pinned_indices) {
            size_t abs_idx = base_index + idx;
            if (abs_idx < replacement.size())
                replacement[abs_idx].preserved = true;
        }

        messages_.swap(replacement);
    }

    // ================================================================
    // 读取（IMemory 核心接口实现）
    // ================================================================

    const std::string& systemPrompt() const override { return system_prompt_; }

    const std::vector<Message>& messages() const override { return messages_; }

    std::vector<Message> all() const override {
        if (system_prompt_.empty()) return messages_;
        std::vector<Message> result;
        result.reserve(1 + messages_.size());
        result.push_back(Message::system(system_prompt_));
        result.insert(result.end(), messages_.begin(), messages_.end());
        return result;
    }

    bool empty() const override { return messages_.empty() && system_prompt_.empty(); }
    size_t size() const override { return messages_.size() + (system_prompt_.empty() ? 0 : 1); }

    const Message& back() const override { return messages_.back(); }
    const Message& front() const override { return messages_.front(); }

    Message at(size_t i) const override {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (i < offset) return Message::system(system_prompt_);
        return messages_[i - offset];
    }

    std::vector<size_t> pinnedIndices() const override {
        std::vector<size_t> result;
        size_t offset = system_prompt_.empty() ? 0 : 1;
        for (size_t i = 0; i < messages_.size(); ++i) {
            if (messages_[i].preserved) result.push_back(offset + i);
        }
        return result;
    }

    // ================================================================
    // 状态管理（IMemory 核心接口实现）
    // ================================================================

    void clear() override {
        messages_.clear();
        system_prompt_.clear();
    }

    void truncateTo(size_t keep_count) override {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (keep_count == 0) {
            messages_.clear();
            system_prompt_.clear();
            return;
        }
        if (keep_count <= offset) {
            messages_.clear();
            return;
        }
        size_t total = offset + messages_.size();
        if (keep_count >= total) return;
        messages_.resize(keep_count - offset);
    }

    void removeOldest(size_t count) override {
        if (count >= messages_.size()) {
            messages_.clear();
        } else {
            messages_.erase(messages_.begin(),
                            messages_.begin() + static_cast<ptrdiff_t>(count));
        }
    }

    void popBack() override { messages_.pop_back(); }

    bool pin(size_t index) override {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        messages_[msg_idx].preserved = true;
        return true;
    }

    bool unpin(size_t index) override {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        messages_[msg_idx].preserved = false;
        return true;
    }

    bool edit(size_t index, std::string new_content) override {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        auto& msg = messages_[msg_idx];
        if (msg.role != MessageRole::User && msg.role != MessageRole::Assistant)
            return false;
        msg.content = std::move(new_content);
        msg.preserved = false;
        return true;
    }

    MemorySnapshot checkpoint() const override {
        return MemorySnapshot{messages_, system_prompt_};
    }

    void restore(const MemorySnapshot& snapshot) override {
        messages_ = snapshot.messages;
        system_prompt_ = snapshot.system_prompt;
    }

    // ================================================================
    // 非虚便捷方法（Memory 特有，不在 IMemory 接口中）
    // ================================================================

    void reserve(size_t n) { messages_.reserve(n); }

    auto begin() const { return messages_.begin(); }
    auto end()   const { return messages_.end(); }

private:
    std::vector<Message> messages_;   // 非 System 消息（User / Assistant / Tool）
    std::string system_prompt_;       // 唯一的一条 System 消息
};

} // namespace llm
