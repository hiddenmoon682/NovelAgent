#pragma once

// 多会话管理器 — 管理多个独立的 Agent 会话实例。
// 每个会话拥有独立的 Agent + Conversation。
//
// 线程安全：每个会话通过 LLMClientFactory 创建独立的 Agent（从而独立的 LLMClient），
// 确保多客户端并发时互不干扰。

#include "agent/core/Agent.h"
#include "agent/core/AgentState.h"
#include "agent/core/ExecutionTracer.h"
#include "agent/tool/ToolRegistry.h"
#include "agent/context/Memory.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Project;

namespace llm {
class LLMClientFactory;
} // namespace llm

namespace agent {

// 单个会话（使用 shared_ptr 管理生命周期，防止 use-after-free）。
struct Session {
    std::string id;
    llm::Memory memory;                          //  每个会话独立的记忆
    std::unique_ptr<agent::Agent> agent;
    std::unique_ptr<agent::StateMachine> state;
    std::chrono::steady_clock::time_point created;
    std::chrono::steady_clock::time_point last_active;
    std::mutex request_mutex;  // 保护并发请求的串行化
};

// 多会话管理器（线程安全）。
class SessionManager {
public:
    // factory  LLM 客户端工厂（每个会话创建独立的 Agent/LLMClient）
    SessionManager(llm::LLMClientFactory& factory, ToolRegistry& registry,
                   std::shared_ptr<Project> project);

    // 创建新会话，返回 session_id。
    std::string createSession();

    // 销毁指定会话。
    void destroySession(const std::string& id);

    // 获取会话的 shared_ptr（持有引用，防止并发销毁）。
    std::shared_ptr<Session> getSession(const std::string& id);

    // 获取所有活跃会话 ID 列表。
    std::vector<std::string> activeSessions() const;

    // 活跃会话数。
    int sessionCount() const;

    // 清理超过 timeout 未活跃的会话。
    void cleanupIdleSessions(std::chrono::minutes timeout);

    // 共享的项目数据。
    std::shared_ptr<Project> project() { return project_; }

private:
    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;
    std::shared_ptr<Project> project_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;

    std::string generateId() const;
};

} // namespace agent
