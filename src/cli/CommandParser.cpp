#include "cli/CommandParser.h"
#include <sstream>

void CommandParser::registerCommand(const std::string& name, std::string help,
                                     CommandHandler handler) {
    commands_.push_back({name, std::move(help), std::move(handler)});
}

bool CommandParser::isCommand(const std::string& input) {
    return !input.empty() && input[0] == '/';
}

bool CommandParser::execute(const std::string& input) {
    std::string cmd_line = input.substr(1);
    std::istringstream ss(cmd_line);
    std::string cmd_name;
    ss >> cmd_name;

    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg) args.push_back(arg);

    auto lower_cmd = cmd_name;
    for (char& ch : lower_cmd) ch = static_cast<char>(std::tolower(ch));
    auto it = std::find_if(commands_.begin(), commands_.end(),
        [&](const Command& c) {
            std::string n = c.name;
            for (char& ch : n) ch = static_cast<char>(std::tolower(ch));
            return n == lower_cmd;
        });

    if (it == commands_.end()) {
        out_.write("未知命令: /" + cmd_name + "（输入 /help 查看可用命令）\n");
        return true;
    }

    return it->handler(args);
}

void CommandParser::printHelp() const {
    std::string help = "\n可用命令:\n";
    for (const auto& cmd : commands_) {
        help += "  /" + cmd.name;
        int pad = 20 - static_cast<int>(cmd.name.size());
        if (pad > 0) help += std::string(pad, ' ');
        help += cmd.help + "\n";
    }
    help += "\n";
    out_.write(help);
}
