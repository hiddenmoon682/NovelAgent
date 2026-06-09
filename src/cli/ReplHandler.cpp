#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "agent/ToolRegistry.h"

#include <iostream>
#include <string>

ReplHandler::ReplHandler(agent::Agent& agent) : agent_(agent) {
    setupCommands();
}

void ReplHandler::setWelcomeMessage(std::string msg) {
    welcome_ = std::move(msg);
}

void ReplHandler::setupCommands() {
    parser_.registerCommand("help", "显示此帮助信息",
        [this](const auto&) { parser_.printHelp(); return true; });

    parser_.registerCommand("exit", "退出程序",
        [](const auto&) { std::cout << "再见！\n"; return false; });

    parser_.registerCommand("clear", "清空对话历史",
        [this](const auto&) {
            agent_.clearConversation();
            std::cout << "对话历史已清空。\n";
            return true;
        });

    // 预留 /model 和 /tools，等后续实现配置切换和工具列表展示
    parser_.registerCommand("tools", "列出当前注册的工具",
        [this](const auto&) {
            // Agent 不直接暴露 ToolRegistry，通过 agent_ 间接获取
            std::cout << "使用 Agent 对话来查询工具列表（未来版本会直接展示）\n";
            return true;
        });

    parser_.registerCommand("model", "显示当前模型信息",
        [this](const auto&) {
            std::cout << "当前模型配置请查看 config.json\n";
            return true;
        });
}

void ReplHandler::run() {
    std::cout << welcome_ << "\n";
    std::cout << "输入 /help 查看可用命令，/exit 退出。\n\n";

    std::string input;
    while (true) {
        std::cout << "\033[1m> \033[0m" << std::flush;
        if (!std::getline(std::cin, input)) break; // EOF
        if (input.empty()) continue;

        if (CommandParser::isCommand(input)) {
            if (!parser_.execute(input)) break;
            continue;
        }

        // 对话内容 → Agent
        std::cout << "\n" << std::flush;
        try {
            auto callbacks = StreamDisplay::create();
            auto response = agent_.processUserMessage(input, callbacks);
            // 非正常结束时提示用户
            if (response.finish_reason == "length") {
                std::cout << "\n  \033[33m[注意: 回复因长度限制被截断]\033[0m";
            } else if (response.finish_reason == "content_filter") {
                std::cout << "\n  \033[33m[注意: 部分内容因安全策略被过滤]\033[0m";
            }
            std::cout << "\n" << std::flush;
        } catch (const std::exception& e) {
            std::cerr << "\n错误: " << e.what() << "\n";
        }
    }
}
