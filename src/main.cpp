// NovelAgent — AI 写小说助手。Phase 5 打磨版。
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
    // Phase 5.1: 启用 Windows ANSI 支持
    Ansi::enableWindowsAnsi();

    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};

    std::string projectPath, execCommand, providerName = "deepseek";
    bool verbose = false;

    app.add_option("-p,--project", projectPath, "项目目录路径");
    app.add_option("-e,--exec", execCommand, "执行单次命令后退出");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "启用调试日志");

    try { app.parse(argc, argv); }
    catch (const CLI::ParseError& e) { return app.exit(e); }

    if (verbose) spdlog::set_level(spdlog::level::debug);

    // Phase 5.3: 全局错误恢复 — 最外层 try/catch
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

        std::cout << Ansi::title() << "NovelAgent v0.3.0" << Ansi::reset()
                  << " | " << Ansi::info() << provider->name << Ansi::reset()
                  << " | " << provider->model << "\n";

        // 打开/创建项目
        ProjectManager pm;
        Project project;
        if (!projectPath.empty()) {
            project = pm.openOrCreate(projectPath);
            if (project.title.empty()) {
                std::cerr << Ansi::error() << "错误: 无法打开/创建项目 "
                          << projectPath << "\n" << Ansi::reset();
                return 1;
            }
            std::cout << "\n" << Ansi::title() << "项目: " << project.title
                      << Ansi::reset()
                      << " | 章节: " << project.outline.chapters.size()
                      << " | 角色: " << project.characters.size() << "\n";
        }

        // 创建应用
        auto projectPtr = std::make_shared<Project>(std::move(project));
        NovelAgentApp novelAgent(*provider, projectPtr);

        // 分发到 REPL 或 --exec
        if (!execCommand.empty()) {
            novelAgent.runExec(execCommand);
        } else if (!projectPath.empty()) {
            novelAgent.runRepl();
        } else {
            std::cout << "使用 -p <目录> 指定项目后进入 REPL。\n";
        }
    } catch (const std::exception& e) {
        std::cerr << Ansi::error() << "\n致命错误: " << e.what()
                  << Ansi::reset() << "\n";
        std::cerr << Ansi::warning()
                  << "程序将退出。项目文件未被修改。\n" << Ansi::reset();
        return 1;
    } catch (...) {
        std::cerr << Ansi::error()
                  << "\n发生未知错误，程序将退出。\n" << Ansi::reset();
        return 1;
    }

    return 0;
}
