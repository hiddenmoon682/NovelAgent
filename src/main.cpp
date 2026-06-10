// NovelAgent — AI 写小说助手。Phase 6 前后端分离版。
// 三种模式: 单体REPL | 后端Server | 单次命令
#include "NovelAgentApp.h"
#include "agent/AgentSetup.h"
#include "cli/AnsiTerminal.h"
#include "config/AppConfig.h"
#include "project/ProjectManager.h"
#include "server/BackendServer.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    Ansi::enableWindowsAnsi();

    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};

    std::string projectPath, backendProjectPath, execCommand, providerName = "deepseek";
    bool verbose = false;
    int wsPort = 8899;

    app.add_option("-p,--project", projectPath, "项目目录路径");
    app.add_option("-e,--exec", execCommand, "执行单次命令后退出");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "启用调试日志");

    // Phase 6: backend 子命令
    auto* backendCmd = app.add_subcommand("backend", "启动后端 HTTP+SSE 服务器");
    backendCmd->add_option("-p,--project", backendProjectPath, "项目目录路径")->required();
    backendCmd->add_option("--port", wsPort, "HTTP 端口 (默认 8899)");
    backendCmd->add_option("--provider", providerName, "LLM provider");
    backendCmd->add_flag("-v,--verbose", verbose, "启用调试日志");

    try { app.parse(argc, argv); }
    catch (const CLI::ParseError& e) { return app.exit(e); }

    if (verbose) spdlog::set_level(spdlog::level::debug);

    try {
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

        // ── backend 模式 ──
        if (app.got_subcommand("backend")) {
            ProjectManager pm;
            Project project = pm.openOrCreate(backendProjectPath);
            if (project.title.empty()) {
                std::cerr << Ansi::error() << "无法打开项目: "
                          << backendProjectPath << "\n" << Ansi::reset();
                return 1;
            }
            auto projectPtr = std::make_shared<Project>(std::move(project));

            // 创建 LLMClient + ToolRegistry
            llm::LLMClient llmClient(*provider);
            agent::ToolRegistry registry;
            agent::registerAllTools(registry, projectPtr);

            server::ServerConfig cfg;
            cfg.port = wsPort;
            cfg.project_path = backendProjectPath;
            server::BackendServer backend(llmClient, registry, projectPtr, cfg);

            std::cout << Ansi::title() << "NovelAgent Backend" << Ansi::reset()
                      << " | " << Ansi::info() << "HTTP+SSE → localhost:"
                      << wsPort << Ansi::reset() << "\n";
            std::cout << Ansi::dim() << "项目: " << projectPtr->title
                      << " | 等待前端连接...\n" << Ansi::reset();
            std::cout << Ansi::dim() << "按 Ctrl+C 停止服务器\n" << Ansi::reset();

            backend.run();
            return 0;
        }

        // ── 打开项目（可选）──
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

        NovelAgentApp novelAgent(*provider, projectPtr);

        if (!execCommand.empty()) {
            novelAgent.runExec(execCommand);
            return 0;
        }

        novelAgent.runRepl();

    } catch (const std::exception& e) {
        std::cerr << Ansi::error() << "\n致命错误: " << e.what()
                  << Ansi::reset() << "\n";
        return 1;
    } catch (...) {
        std::cerr << Ansi::error() << "\n发生未知错误，程序将退出。\n" << Ansi::reset();
        return 1;
    }

    return 0;
}
