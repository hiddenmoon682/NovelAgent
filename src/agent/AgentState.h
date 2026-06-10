#pragma once

/// Agent 显式状态机 — Phase 5.4。
///
/// 使 Agent 的状态转换可追踪、可查询、可日志化。
/// StreamDisplay 根据状态切换界面提示（如 "Thinking..."、"Executing tool..."）。

#include <string>

namespace agent {

/// Agent 状态枚举。
enum class AgentState {
    Idle,            // 等待用户输入
    Thinking,        // LLM 思考中（流式输出）
    AwaitingTool,    // 工具执行中
    WaitingUser,     // 等待用户确认（Plan Mode / 危险操作确认）
    Error,           // 错误状态（可恢复）
    Fatal            // 不可恢复错误
};

/// 状态 → 中文描述。
inline const char* agentStateName(AgentState s) {
    switch (s) {
        case AgentState::Idle:         return "就绪";
        case AgentState::Thinking:     return "思考中";
        case AgentState::AwaitingTool: return "执行工具";
        case AgentState::WaitingUser:  return "等待确认";
        case AgentState::Error:        return "错误";
        case AgentState::Fatal:        return "致命错误";
    }
    return "未知";
}

/// 状态机 — 管理 Agent 状态转换。
///
/// 使用示例:
///   StateMachine sm;
///   sm.transition(AgentState::Thinking);  // Idle → Thinking
///   sm.transition(AgentState::Idle);      // Thinking → Idle
class StateMachine {
public:
    StateMachine() = default;

    /// 当前状态。
    AgentState current() const { return state_; }

    /// 转换到新状态。返回 true 表示合法转换。
    bool transition(AgentState new_state);

    /// 是否处于可接受用户输入的状态。
    bool canAcceptInput() const {
        return state_ == AgentState::Idle || state_ == AgentState::WaitingUser;
    }

    /// 是否处于错误状态。
    bool isError() const {
        return state_ == AgentState::Error || state_ == AgentState::Fatal;
    }

    /// 从可恢复错误中恢复。
    void recover() {
        if (state_ == AgentState::Error) state_ = AgentState::Idle;
    }

private:
    AgentState state_ = AgentState::Idle;
};

} // namespace agent
