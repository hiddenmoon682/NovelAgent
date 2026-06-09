#pragma once

#include <functional>
#include <string>
#include <vector>

/// REPL 命令解析器。
/// 以 '/' 开头的输入被视为命令，其余为对话内容。
/// 返回 true 表示继续 REPL，false 表示退出。
class CommandParser {
public:
    /// 命令处理回调类型。
    /// @return true 继续 REPL / false 退出
    using CommandHandler = std::function<bool(const std::vector<std::string>& args)>;

    /// 注册一个命令。
    void registerCommand(const std::string& name, std::string help,
                         CommandHandler handler);

    /// 判断是否为命令（以 '/' 开头）。
    static bool isCommand(const std::string& input);

    /// 执行命令。返回 true 继续 REPL，false 退出。
    /// @param input  原始输入（含 '/'）
    bool execute(const std::string& input);

    /// 打印帮助信息。
    void printHelp() const;

private:
    struct Command {
        std::string name;
        std::string help;
        CommandHandler handler;
    };
    std::vector<Command> commands_;
};
