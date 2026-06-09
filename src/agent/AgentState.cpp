/// StateMachine 实现。

#include "agent/AgentState.h"

#include <spdlog/spdlog.h>

namespace agent {

bool StateMachine::transition(AgentState new_state) {
    AgentState old = state_;

    // 合法性检查
    switch (new_state) {
        case AgentState::Idle:
        case AgentState::Thinking:
        case AgentState::AwaitingTool:
        case AgentState::WaitingUser:
        case AgentState::Error:
        case AgentState::Fatal:
            break;
    }

    if (old != new_state) {
        spdlog::info("[Agent] 状态转换: {} → {}",
                     agentStateName(old), agentStateName(new_state));
    }

    state_ = new_state;
    return true;
}

} // namespace agent
