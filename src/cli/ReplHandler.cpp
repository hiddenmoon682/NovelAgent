#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"

#include <iostream>
#include <string>

ReplHandler::ReplHandler(agent::Agent& agent, IOutputChannel& out)
    : agent_(agent), out_(out), parser_(out) {
    setupCommands();
}

void ReplHandler::setWelcomeMessage(std::string msg) {
    welcome_ = std::move(msg);
}

void ReplHandler::setupCommands() {
    parser_.registerCommand("help", "显示此帮助信息",
        [this](const auto&) { parser_.printHelp(); return true; });

    parser_.registerCommand("exit", "退出程序",
        [this](const auto&) { out_.write("再见！\n"); return false; });

    parser_.registerCommand("clear", "清空对话历史",
        [this](const auto&) {
            agent_.clearConversation();
            out_.write("对话历史已清空。\n");
            return true;
        });

    parser_.registerCommand("tools", "列出当前注册的工具",
        [this](const auto&) {
            out_.write("使用 Agent 对话来查询工具列表（未来版本会直接展示）\n");
            return true;
        });

    parser_.registerCommand("model", "显示当前模型信息",
        [this](const auto&) {
            out_.write("当前模型配置请查看 config.json\n");
            return true;
        });
}

void ReplHandler::run() {
    out_.write(welcome_ + "\n");
    out_.write("输入 /help 查看可用命令，/exit 退出。\n\n");

    std::string input;
    while (true) {
        // prompt 仍用 std::cout（需要用户输入）
        std::cout << "\033[1m> \033[0m" << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (CommandParser::isCommand(input)) {
            if (!parser_.execute(input)) break;
            continue;
        }

        out_.write("\n");
        try {
            auto callbacks = StreamDisplay::create(out_);
            auto response = agent_.processUserMessage(input, callbacks);
            if (response.finish_reason == "length") {
                out_.write("\n  \033[33m[注意: 回复因长度限制被截断]\033[0m");
            } else if (response.finish_reason == "content_filter") {
                out_.write("\n  \033[33m[注意: 部分内容因安全策略被过滤]\033[0m");
            }
            out_.write("\n");
        } catch (const std::exception& e) {
            out_.writeError("\n错误: " + std::string(e.what()) + "\n");
        }
    }
}
