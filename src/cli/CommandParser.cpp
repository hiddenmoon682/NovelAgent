#include "cli/CommandParser.h"
#include <algorithm>
#include <iostream>
#include <sstream>

void CommandParser::registerCommand(const std::string& name, std::string help,
                                     CommandHandler handler) {
    commands_.push_back({name, std::move(help), std::move(handler)});
}

bool CommandParser::isCommand(const std::string& input) {
    return !input.empty() && input[0] == '/';
}

bool CommandParser::execute(const std::string& input) {
    // 去掉开头的 '/'
    std::string cmd_line = input.substr(1);
    std::istringstream ss(cmd_line);
    std::string cmd_name;
    ss >> cmd_name;

    // 收集参数
    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg) args.push_back(arg);

    auto it = std::find_if(commands_.begin(), commands_.end(),
        [&](const Command& c) { return c.name == cmd_name; });

    if (it == commands_.end()) {
        std::cout << "未知命令: /" << cmd_name << "（输入 /help 查看可用命令）\n";
        return true;
    }

    return it->handler(args);
}

void CommandParser::printHelp() const {
    std::cout << "\n可用命令:\n";
    for (const auto& cmd : commands_) {
        std::cout << "  /" << cmd.name;
        // 对齐
        int pad = 20 - static_cast<int>(cmd.name.size());
        if (pad > 0) std::cout << std::string(pad, ' ');
        std::cout << cmd.help << "\n";
    }
    std::cout << "\n";
}
