#pragma once

#include "agent/Agent.h"
#include "cli/CommandParser.h"
#include "cli/IOutputChannel.h"
#include <string>

namespace agent { class AgentOrchestrator; }

class ReplHandler {
public:
    /// @param agent         已配置好的 Agent 实例
    /// @param out           输出通道
    /// @param orchestrator  可选：并行编排器（设置后自动检测并行任务）
    ReplHandler(agent::Agent& agent, IOutputChannel& out,
                agent::AgentOrchestrator* orchestrator = nullptr);

    void run();
    void setWelcomeMessage(std::string msg);

private:
    agent::Agent& agent_;
    IOutputChannel& out_;
    CommandParser parser_;
    std::string welcome_;
    agent::AgentOrchestrator* orchestrator_;

    void setupCommands();
};
