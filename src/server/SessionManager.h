#pragma once

/// 多会话管理器 — 管理多个独立的 Agent 会话实例。
/// 每个前端连接对应一个 Session，拥有独立的 Agent + Conversation。

#include "agent/Agent.h"
#include "agent/AgentState.h"
#include "agent/ContextManager.h"
#include "agent/ExecutionTracer.h"
#include "agent/ToolRegistry.h"
#include "llm/ILLMClient.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Project;

namespace server {

/// 单个会话（使用 shared_ptr 管理生命周期，防止 use-after-free）。
struct Session {
    std::string id;
    std::unique_ptr<agent::Agent> agent;
    std::unique_ptr<agent::ContextManager> ctx_mgr;
    std::unique_ptr<agent::StateMachine> state;
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point last_active;
};

/// 多会话管理器（线程安全）。
class SessionManager {
public:
    SessionManager(llm::ILLMClient& client, agent::ToolRegistry& registry,
                   std::shared_ptr<Project> project);

    /// 创建新会话，返回 session_id。
    std::string createSession();

    /// 销毁指定会话。
    void destroySession(const std::string& id);

    /// 获取会话的 shared_ptr（持有引用，防止并发销毁）。
    std::shared_ptr<Session> getSession(const std::string& id);

    /// 获取所有活跃会话 ID 列表。
    std::vector<std::string> activeSessions() const;

    /// 活跃会话数。
    int sessionCount() const;

    /// 清理超过 timeout 未活跃的会话。
    void cleanupIdleSessions(std::chrono::minutes timeout);

    /// 共享的项目数据。
    std::shared_ptr<Project> project() { return project_; }

private:
    llm::ILLMClient& client_;
    agent::ToolRegistry& registry_;
    std::shared_ptr<Project> project_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;

    std::string generateId() const;
};

} // namespace server
