#pragma once

// IMemory — 记忆抽象接口，定义 Agent 与 Memory 之间的对等协作契约。
//
// Agent 通过注入（inject/apply）写入上下文，通过读取方法查询上下文，
// 通过 checkpoint/restore 管理快照回滚。Memory 自治管理内部状态。
//
// 设计原则：
//   - 读取方法返回 const 引用或值，不暴露内部可变状态
//   - 写入方法统一为"注入"语义（inject/apply/prepend）
//   - checkpoint/restore 替代外部手动拷贝，由 Memory 自治管理快照

#include "llm/Message.h"

#include <string>
#include <vector>

namespace llm {

// 记忆修改的批量描述 — 将多次 inject/pin 合并为一次原子操作。
// 生产者（ToolPipeline/SubAgent）返回 diff，消费者（Memory）统一 apply。
struct MemoryDiff {
    std::vector<Message> added;          //  按顺序追加的消息
    std::vector<size_t> pinned_indices;  //  需 pin 的消息在 diff.added 中的索引（非全局索引），apply() 自动加 base_offset
    bool retryable = false;              //  错误是否可重试（由 ToolPipeline 设置）
};

// 记忆快照 — checkpoint() 返回，restore() 消费。
// 替代 Agent 手动拷贝整个 Conversation 的做法，由 Memory 自治管理。
struct MemorySnapshot {
    std::vector<Message> messages;
    std::string system_prompt;
};

// 记忆抽象接口 — Agent 与 Memory 对等协作的契约。
//
// 三组方法：
//   1. 读取：messages(), systemPrompt(), size(), pinnedIndices() 等
//   2. 注入：inject(Message), setSystemPrompt(), apply(MemoryDiff), prepend()
//   3. 状态管理：clear(), truncateTo(), pin(), checkpoint(), restore() 等
class IMemory {
public:
    virtual ~IMemory() = default;

    // ================================================================
    // 读取（Agent 查询上下文）
    // ================================================================

    // 获取不含 system 消息的对话历史（传给 LLMClient::chat）。
    // 零拷贝 — 直接返回内部引用。仅限"本记忆的单写线程"（每会话的池工作线程）调用。
    virtual const std::vector<Message>& messages() const = 0;

    // 加锁拷贝的对话历史快照（不含 system 消息）。
    // 跨线程读取（GUI 线程读运行中会话的 memory）必须走此接口：
    // messages() 的裸引用与池线程的 vector 变异并发迭代是数据竞争（UB）。
    virtual std::vector<Message> snapshot() const = 0;

    // 返回系统提示词。
    virtual const std::string& systemPrompt() const = 0;

    // 获取全部消息（含 system 消息），用于持久化和调试。
    // 返回值为临时 vector（拼接 system_prompt_ + messages_），仅在低频路径使用。
    virtual std::vector<Message> all() const = 0;

    // 消息总数（system prompt 存在时计入 1 条）。
    virtual size_t size() const = 0;
    // 是否既无对话消息也无 system prompt。
    virtual bool empty() const = 0;
    // 最后/最早一条非 system 消息（前置条件：messages() 非空）。
    virtual const Message& back() const = 0;
    virtual const Message& front() const = 0;

    // 按 all() 视角索引访问消息（0 = system_prompt_ 若存在，1+ = messages_）。
    virtual Message at(size_t index) const = 0;

    // 获取所有保留消息的索引（all() 视角）。
    virtual std::vector<size_t> pinnedIndices() const = 0;

    // ================================================================
    // 注入（Agent 写入上下文）
    // ================================================================

    // 追加一条消息（通用注入口）。System 角色消息存入 system_prompt_，其余追加到尾部。
    virtual void inject(Message msg) = 0;

    // 设置系统提示词（替换旧的）。
    virtual void setSystemPrompt(std::string prompt) = 0;

    // 批量原子修改。采用 copy-then-swap 模式提供强异常保证。
    virtual void apply(MemoryDiff diff) = 0;

    // 在对话头部插入一条消息（压缩摘要用）。
    virtual void prepend(Message msg) = 0;

    // ================================================================
    // 状态管理（Memory 自治）
    // ================================================================

    // 清空全部对话消息与 system prompt。
    virtual void clear() = 0;

    // 截断到前 N 条消息（保留 [0, keep_count)），丢弃其余。
    // keep_count 计数包含 system_prompt_（如果存在）。
    virtual void truncateTo(size_t keep_count) = 0;

    // 从头部删除 count 条非 system 消息（最旧的）。
    virtual void removeOldest(size_t count) = 0;

    // 移除最后一条非 system 消息（前置条件：messages() 非空）。
    virtual void popBack() = 0;

    // 消息保留标记（Pin），index 为 all() 视角索引。
    virtual bool pin(size_t index) = 0;
    virtual bool unpin(size_t index) = 0;

    // 编辑指定索引的消息内容（仅允许 User 和 Assistant 消息）。
    virtual bool edit(size_t index, std::string new_content) = 0;

    // 快照与恢复 — 替代外部手动拷贝，由 Memory 自治管理。
    virtual MemorySnapshot checkpoint() const = 0;
    virtual void restore(const MemorySnapshot& snapshot) = 0;

    // ================================================================
    // 便捷方法（非虚，构造 Message 后委托到 inject）
    // ================================================================

    void addUser(std::string content) { inject(Message::user(std::move(content))); }
    void addAssistant(std::string content) { inject(Message::assistant(std::move(content))); }
    void addToolResult(std::string call_id, std::string content) {
        inject(Message::toolResult(std::move(call_id), std::move(content)));
    }
    Message operator[](size_t i) const { return at(i); }
};

} // namespace llm
