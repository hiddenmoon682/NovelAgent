#pragma once

#include "agent/Agent.h"
#include "cli/CommandParser.h"
#include <string>

/// REPL 交互循环处理器。
class ReplHandler {
public:
    /// @param agent  已配置好的 Agent 实例
    explicit ReplHandler(agent::Agent& agent);

    /// 启动 REPL 主循环（阻塞，直到用户输入 /exit）。
    void run();

    /// 设置欢迎信息。
    void setWelcomeMessage(std::string msg);

private:
    agent::Agent& agent_;
    CommandParser parser_;
    std::string welcome_;

    /// 初始化内置命令。
    void setupCommands();
};
