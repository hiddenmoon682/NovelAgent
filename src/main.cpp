// NovelAgent — AI 写小说助手。Phase 5 打磨版。
// 直接运行 novelagent.exe 即可进入 Claude Code 风格的终端 GUI。
#include "NovelAgentApp.h"
#include "cli/AnsiTerminal.h"
#include "config/AppConfig.h"
#include "project/ProjectManager.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    Ansi::enableWindowsAnsi();

    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};
    app.set_help_flag("-h,--help", "打印帮助信息");

    std::string projectPath, execCommand, providerName = "deepseek";
    bool verbose = false;

    // -p 和 -e 现在是可选参数
    app.add_option("-p,--project", projectPath, "项目目录路径（可选，不指定则进入欢迎页）");
    app.add_option("-e,--exec", execCommand, "执行单次命令后退出");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "启用调试日志");

    try { app.parse(argc, argv); }
    catch (const CLI::ParseError& e) { return app.exit(e); }

    if (verbose) spdlog::set_level(spdlog::level::debug);

    try {
        // 加载配置 + 环境变量覆盖
        AppConfig config = AppConfig::load();
        for (const auto& name : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
            if (const char* key = std::getenv((std::string(name) + "_API_KEY").c_str())) {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                config.setApiKey(lower, key);
            }
        }

        auto* provider = config.getProvider(providerName);
        if (!provider) {
            std::cerr << Ansi::error() << "错误: 未找到 provider '"
                      << providerName << "'\n" << Ansi::reset();
            return 1;
        }

        // 打开/创建项目（可选）
        std::shared_ptr<Project> projectPtr;
        if (!projectPath.empty()) {
            ProjectManager pm;
            Project project = pm.openOrCreate(projectPath);
            if (project.title.empty()) {
                std::cerr << Ansi::error() << "错误: 无法打开/创建项目 "
                          << projectPath << "\n" << Ansi::reset();
                return 1;
            }
            projectPtr = std::make_shared<Project>(std::move(project));
        }

        // 创建应用
        NovelAgentApp novelAgent(*provider, projectPtr);

        // --exec 单次命令模式
        if (!execCommand.empty()) {
            novelAgent.runExec(execCommand);
            return 0;
        }

        // REPL 交互模式（有项目直接进入，无项目显示欢迎页+引导命令）
        novelAgent.runRepl();

    } catch (const std::exception& e) {
        std::cerr << Ansi::error() << "\n致命错误: " << e.what()
                  << Ansi::reset() << "\n";
        std::cerr << Ansi::warning()
                  << "程序将退出，项目文件未被修改。\n" << Ansi::reset();
        return 1;
    } catch (...) {
        std::cerr << Ansi::error()
                  << "\n发生未知错误，程序将退出。\n" << Ansi::reset();
        return 1;
    }

    return 0;
}
