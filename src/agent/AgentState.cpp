// StateMachine 实现。

#include "agent/AgentState.h"

#include <spdlog/spdlog.h>

namespace agent {

namespace {

// 判断状态转换是否合法。
//
// 状态转换图：
//   Idle ──────────→ Thinking ──────────→ AwaitingTool
//    │   ←──────────    │    ←──────────    │
//    │                  │                   │
//    ├──→ WaitingUser ←─┘                   │
//    │   ←──                               │
//    │                                      │
//    ├──→ Fatal ←── Thinking / AwaitingTool / Error
//    │   ←── (reset)
//    │
//    └──→ Error ←── Thinking / AwaitingTool
//         ←── (recover)
bool isValidTransition(AgentState from, AgentState to) {
    // 同状态转换——无害，允许
    if (from == to) return true;

    switch (from) {
        case AgentState::Idle:
            // 用户发送消息 → Thinking，系统等待确认 → WaitingUser
            // 启动阶段致命错误 → Fatal
            return to == AgentState::Thinking
                || to == AgentState::WaitingUser
                || to == AgentState::Fatal;

        case AgentState::Thinking:
            // 正常完成 → Idle，LLM 请求工具 → AwaitingTool
            // 调用失败 → Error，不可恢复 → Fatal
            return to == AgentState::Idle
                || to == AgentState::AwaitingTool
                || to == AgentState::Error
                || to == AgentState::Fatal;

        case AgentState::AwaitingTool:
            // 工具返回继续 LLM → Thinking，工具失败 → Error
            // 不可恢复 → Fatal，用户中断 → Idle
            return to == AgentState::Thinking
                || to == AgentState::Error
                || to == AgentState::Fatal
                || to == AgentState::Idle;

        case AgentState::WaitingUser:
            // 用户确认/输入 → Thinking，取消/超时 → Idle
            return to == AgentState::Thinking
                || to == AgentState::Idle;

        case AgentState::Error:
            // 可恢复 → Idle，不可恢复 → Fatal
            return to == AgentState::Idle
                || to == AgentState::Fatal;

        case AgentState::Fatal:
            // 只能重置 → Idle
            return to == AgentState::Idle;
    }
    return false; // 不可达
}

} // anonymous namespace

bool StateMachine::transition(AgentState new_state) {
    AgentState old = state_;

    // 编译期枚举穷举检查（-Wswitch）：不写 default 分支，
    // 当 AgentState 新增枚举值时编译器会警告此处未处理，
    // 强制开发者同步更新 isValidTransition()。
    switch (new_state) {
        case AgentState::Idle:
        case AgentState::Thinking:
        case AgentState::AwaitingTool:
        case AgentState::WaitingUser:
        case AgentState::Error:
        case AgentState::Fatal:
            break;
    }

    // 运行时合法性检查
    if (!isValidTransition(old, new_state)) {
        spdlog::warn("[Agent] 非法状态转换被拒绝: {} → {}",
                     agentStateName(old), agentStateName(new_state));
        return false;
    }

    if (old != new_state) {
        spdlog::info("[Agent] 状态转换: {} → {}",
                     agentStateName(old), agentStateName(new_state));
        state_ = new_state;
    }

    return true;
}

} // namespace agent
