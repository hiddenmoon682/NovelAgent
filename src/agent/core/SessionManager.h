#pragma once

// SessionManager — 从 Agent 提取的会话管理职责（SRP 拆分）。
//
// 职责边界：
//   - 会话生命周期：新建（resetSession）、切换（switchSession）、
//     删除（deleteSession）与边界处的消息重载（reloadActiveSession）。
//   - 会话持久化：saveSessionState / loadSessionState（委托 SessionPersistence）。
//   - 消息级操作：pin/unpin/edit、对话回滚（rewindTo）与检查点查询
//     （checkpointIndices）。
//
// 不属于本类的职责（仍留在 Agent）：
//   - LLM 编排（process/execute/CoreLoop）。
//   - 上下文管理（压缩 compact、预算评估、用量快照 refreshUsage）。
//   - 取消与状态机。
//
// 与 Agent 的协作方式：
//   - 共享同一个 llm::IMemory 引用（构造注入），消息状态只有一份。
//   - 会话边界需要清理 Agent 侧运行时状态（tracer / warnings / 渐进工具），
//     以及加载后刷新上下文用量快照——这些属于 Agent 的职责，通过
//     setBoundaryResetHook / setUsageRefreshHook 回调注入，避免本类
//     反向依赖 Agent 内部结构。

#include "agent/context/IMemory.h"

#include <functional>
#include <string>
#include <vector>

namespace agent {

class SessionPersistence;

// 会话管理器。持有 Agent 共享的 IMemory 引用，封装会话生命周期、
// 持久化与消息级操作；不感知 LLM 编排与上下文压缩。
class SessionManager {
public:
    // @param memory Agent 共享的记忆引用；非拥有，调用方保证其存活期
    //               覆盖本类生命周期。
    explicit SessionManager(llm::IMemory& memory) : memory_(memory) {}

    // 注入会话持久化（非拥有指针，可选；调用方保证存活期。
    // 未注入时会话相关操作降级为纯内存行为/空操作）。
    void setPersistence(SessionPersistence* p) { persistence_ = p; }
    // 持久化访问器（会话列表查询用；未注入时返回 nullptr）。
    SessionPersistence* persistence() { return persistence_; }

    // 注入 system prompt 提供者（可选）。会话边界（新建/切换/删除重载）
    // 时重新生成 prompt，使运行期变化的成分（如 save_skill 新增的技能目录）
    // 在下个会话生效；会话中途不重建，保持 KV cache 稳定。
    void setSystemPromptProvider(std::function<std::string()> provider) {
        system_prompt_provider_ = std::move(provider);
    }

    // 注入会话边界清理回调（Agent 用它清理 tracer/warnings/渐进工具等
    // 运行时状态）。在新建/切换/删除重载时、清空 memory 之后调用。
    void setBoundaryResetHook(std::function<void()> hook) {
        boundary_reset_hook_ = std::move(hook);
    }
    // 注入用量刷新回调（Agent 用它重算上下文用量快照）。
    // 在 loadSessionState 末尾与 resetSession 末尾调用。
    void setUsageRefreshHook(std::function<void()> hook) {
        usage_refresh_hook_ = std::move(hook);
    }

    // ── 会话生命周期 ──

    // 新建会话：保存当前会话（保留在列表中），创建空会话并切换。
    // 当前会话为空时不新建（避免堆积空会话），仅重置运行时状态。
    void resetSession();
    // 切换到指定会话：保存当前会话后重载目标会话的消息。
    bool switchSession(const std::string& id);
    // 删除指定会话（持久层负责归档）；删除的是 active 会话时自动重载新 active。
    bool deleteSession(const std::string& id);

    // ── 会话持久化 ──

    // 全量保存当前对话到 active 会话文件；未注入持久化时为空操作。
    void saveSessionState();
    // 从 active 会话恢复对话消息；system prompt 以内存中当前装配的为准。
    // 启动路径安全：恢复失败降级为空会话，不抛异常；末尾触发用量刷新回调。
    void loadSessionState();

    // ── 消息级操作（index 均为 all() 视角索引，非法索引返回 false）──

    // 标记/取消标记消息为保留（pin）；标记随会话持久化，跨重启保留。
    bool pinMessage(size_t index);
    bool unpinMessage(size_t index);
    // 编辑消息内容（仅限 User/Assistant 消息，编辑后清除 pin 标记）。
    bool editMessage(size_t index, std::string new_content);

    // ── 对话回滚 ──

    // 回滚对话到指定消息（保留 [0, index]）；index 越界返回 false。
    // index 之后的 pinned 消息也会被丢弃（记警告日志）。
    bool rewindTo(size_t index);
    // 全部用户消息的索引（all() 视角），作为可回滚检查点供 UI 展示。
    std::vector<size_t> checkpointIndices() const;

private:
    // 清空运行时状态并从 active 会话重载消息（切换/删除会话后使用）。
    void reloadActiveSession();

private:
    llm::IMemory& memory_;
    SessionPersistence* persistence_ = nullptr;
    std::function<std::string()> system_prompt_provider_;  // 会话边界 prompt 重建
    std::function<void()> boundary_reset_hook_;            // Agent 运行时状态清理
    std::function<void()> usage_refresh_hook_;             // Agent 用量快照刷新
};

} // namespace agent
