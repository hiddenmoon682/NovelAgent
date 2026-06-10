#pragma once

#include "cli/IOutputChannel.h"
#include <functional>
#include <string>
#include <vector>

class CommandParser {
public:
    using CommandHandler = std::function<bool(const std::vector<std::string>& args)>;

    explicit CommandParser(IOutputChannel& out) : out_(out) {}

    void registerCommand(const std::string& name, std::string help, CommandHandler handler);
    static bool isCommand(const std::string& input);
    bool execute(const std::string& input);
    void printHelp() const;

private:
    IOutputChannel& out_;
    struct Command { std::string name, help; CommandHandler handler; };
    std::vector<Command> commands_;
};
