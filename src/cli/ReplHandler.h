#pragma once

#include "agent/Agent.h"
#include "cli/CommandParser.h"
#include "cli/IOutputChannel.h"
#include <string>

class ReplHandler {
public:
    ReplHandler(agent::Agent& agent, IOutputChannel& out);

    void run();
    void setWelcomeMessage(std::string msg);

private:
    agent::Agent& agent_;
    IOutputChannel& out_;
    CommandParser parser_;
    std::string welcome_;

    void setupCommands();
};
