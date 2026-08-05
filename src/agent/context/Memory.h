#pragma once

// Memory — 记忆状态管理器（原 Conversation），实现 IMemory 接口。
//
// System 消息独立存储为 system_prompt_（只有一条），其余角色（User/Assistant/Tool）
// 存储在 messages_ 中，因此 messages() 直接返回内部引用无需拷贝过滤。
//
// 使用示例：
//   Memory mem;
//   mem.setSystemPrompt("你是一个有用的助手。");
//   mem.addUser("你好");
//   mem.addAssistant("你好！有什么可以帮助你的？");
//
//   // 传给 LLMClient（messages() 零拷贝）
//   client.chat(mem.messages(), {}, mem.systemPrompt());
//
// 注意：Memory 不负责持久化——序列化由 Agent 层处理。

#include "agent/context/IMemory.h"
#include "llm/Message.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

namespace llm {

class Memory : public IMemory {
public:
    // WHY：自动 pin（ToolPipeline 对设定类工具结果的自动保留）在长篇创作中
    // 只增不减，且 Compactor 按工具组整组保留会放大占用，极端时压缩区间
    // 全部被 pin → 压缩失效 → 提前触发 context_overflow。因此对自动 pin 设
    // 数量上限：7 种设定类工具各保留一条最新结果，再留约 5 条冗余（同一
    // 工具对多个实体的近期调用），取 12；按 ToolPipeline::kMaxContentChars
    // 的截断上限估算，12 条工具结果的 token 占用仍在可腾挪范围内。超限时
    // FIFO 解除最旧的自动 pin——设定类消息有时效性语义，同一实体的最新
    // update 比旧 update 重要。解除只清 preserved 标记、不删消息，交由后续
    // 压缩自然回收。手动 pin（pin()，用户显式意图）不受此上限约束。
    static constexpr size_t kMaxAutoPinned = 12;

    // ================================================================
    // 注入（IMemory 核心接口实现）
    // ================================================================

    void inject(Message msg) override {
        if (msg.role == MessageRole::System) {
            system_prompt_ = std::move(msg.content);
        } else {
            messages_.push_back(std::move(msg));
            // WHY：注入时已带 preserved 标记的 Tool 消息现实中只来自自动 pin
            //（手动 pin 目前无 QML 入口，且典型对象是 user/assistant 消息）。
            // 压缩重建（Agent::applyCompaction 走 clear+inject）与会话解析
            //（SessionPersistence::parseMessages 走 inject）都经由此处重新注入
            // pinned 消息，借此重建自动 pin 追踪队列，保证消息列表整体重建后
            // 上限机制依然生效。副作用：手动 pin 的 Tool 消息在重建后会被重新
            // 归类为自动 pin（当前无手动 pin 入口，可接受的已知取舍）。
            const Message& added = messages_.back();
            if (added.preserved && added.role == MessageRole::Tool &&
                !added.tool_call_id.empty()) {
                trackAutoPin(added.tool_call_id);
                enforceAutoPinLimit();
            }
        }
    }

    void setSystemPrompt(std::string prompt) override {
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

        // WHY：经 MemoryDiff::pinned_indices 到达的 pin 全部由机器产生
        //（ToolPipeline 自动 pin 设定类工具结果），是自动 pin 的唯一生产
        // 路径，在此注册进追踪队列并执行上限检查；手动 pin 走 pin()，
        // 不经此路径、不受上限约束。
        if (!diff.pinned_indices.empty()) {
            for (auto idx : diff.pinned_indices) {
                size_t abs_idx = base_index + idx;
                if (abs_idx >= messages_.size()) continue;
                const auto& m = messages_[abs_idx];
                if (m.role == MessageRole::Tool && !m.tool_call_id.empty())
                    trackAutoPin(m.tool_call_id);
            }
            enforceAutoPinLimit();
        }
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
        // WHY：system_prompt_ 不存在 messages_[0] 而是单独存储，目的是让
        // messages() 能零拷贝直传 LLMClient::chat（无需每次过滤 system 消息），
        // 且替换 prompt 时不碰消息数组。代价是对外的 all() 视角索引与内部
        // messages_ 下标差一个 offset：有 system prompt 时索引 0 映射到它，
        // 其余索引整体后移一位（pin/unpin/edit 同理）。
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
        auto_pin_ids_.clear();
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
        // WHY：手动 pin 表达用户显式意图，不受自动 pin 上限约束；若该消息
        // 此前被自动 pin 追踪，从队列移除（提升为手动），避免被 FIFO 解除。
        if (!messages_[msg_idx].tool_call_id.empty())
            untrackAutoPin(messages_[msg_idx].tool_call_id);
        return true;
    }

    bool unpin(size_t index) override {
        size_t offset = system_prompt_.empty() ? 0 : 1;
        if (index < offset) return false;
        size_t msg_idx = index - offset;
        if (msg_idx >= messages_.size()) return false;
        messages_[msg_idx].preserved = false;
        // 解除 pin 后同步移出自动 pin 队列，避免残留条目占用上限名额
        if (!messages_[msg_idx].tool_call_id.empty())
            untrackAutoPin(messages_[msg_idx].tool_call_id);
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
        // WHY：快照不携带自动 pin 队列（MemorySnapshot 保持消息语义纯粹），
        // 消息列表整体替换后旧队列随之失效，按"preserved 的 Tool 消息即
        // 自动 pin"（与 inject 同一启发式）依消息顺序重建，并立即执行上限
        // 检查——旧会话文件可能累积了超限的自动 pin（本机制上线前的存量），
        // 会话加载走 restore（见 SessionPool::loadSessionState），借此
        // 一次性收敛到上限内，天然兼容旧格式。
        auto_pin_ids_.clear();
        for (const auto& m : messages_) {
            if (m.preserved && m.role == MessageRole::Tool && !m.tool_call_id.empty())
                trackAutoPin(m.tool_call_id);
        }
        enforceAutoPinLimit();
    }

    // ================================================================
    // 非虚便捷方法（Memory 特有，不在 IMemory 接口中）
    // ================================================================

    void reserve(size_t n) { messages_.reserve(n); }

    auto begin() const { return messages_.begin(); }
    auto end()   const { return messages_.end(); }

private:
    // ================================================================
    // 自动 pin 上限管理（私有实现，不进 IMemory 接口）
    // ================================================================

    // 注册一条自动 pin（FIFO 顺序即注册顺序）。
    //
    // WHY：用 tool_call_id 这一稳定标识而非消息下标做追踪——压缩、回滚、
    // 会话重载都会重建/截断消息列表使下标整体失效，而 tool_call_id 随消息
    // 本体一同拷贝与持久化，天然免疫索引失效问题（自动 pin 的对象只会是
    // 携带 tool_call_id 的 Tool 消息，id 由 API 生成保证唯一）。
    void trackAutoPin(const std::string& id) {
        // 幂等：重复注入（如压缩重建时 clear+inject）不产生重复条目
        if (std::find(auto_pin_ids_.begin(), auto_pin_ids_.end(), id) !=
            auto_pin_ids_.end())
            return;
        auto_pin_ids_.push_back(id);
    }

    void untrackAutoPin(const std::string& id) {
        std::erase(auto_pin_ids_, id);
    }

    // 执行自动 pin 上限：先惰性剔除失效条目（消息已被压缩/回滚移除，或已
    // 在别处解除 pin），再按 FIFO 解除最旧的自动 pin 直到不超过上限。
    // 解除只清 preserved 标记，消息本身不删，后续压缩自然回收其空间。
    void enforceAutoPinLimit() {
        std::erase_if(auto_pin_ids_, [this](const std::string& id) {
            return findPreservedTool(id) == nullptr;
        });
        while (auto_pin_ids_.size() > kMaxAutoPinned) {
            const std::string id = auto_pin_ids_.front();
            auto_pin_ids_.pop_front();
            if (Message* msg = findPreservedTool(id)) {
                msg->preserved = false;
                spdlog::info("[Memory] 自动 pin 数量超过上限 {}，FIFO 解除最旧"
                             "的自动 pin (tool_call_id={})，等待压缩回收",
                             kMaxAutoPinned, id);
            }
        }
    }

    // 按 tool_call_id 查找仍处于 pinned 状态的 Tool 消息；未找到返回 nullptr。
    Message* findPreservedTool(const std::string& id) {
        for (auto& m : messages_) {
            if (m.role == MessageRole::Tool && m.preserved && m.tool_call_id == id)
                return &m;
        }
        return nullptr;
    }

    std::vector<Message> messages_;   // 非 System 消息（User / Assistant / Tool）
    std::string system_prompt_;       // 唯一的一条 System 消息
    std::deque<std::string> auto_pin_ids_;  // 自动 pin 的 tool_call_id（FIFO，最旧在前）
};

} // namespace llm
