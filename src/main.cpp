// NovelAgent — AI 写小说助手。
// 使用 -p 指定项目目录进入 REPL，-e 执行单次命令。
#include "NovelAgentApp.h"
#include "config/AppConfig.h"
#include "project/ProjectManager.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
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
        std::cerr << "错误: 未找到 provider '" << providerName << "'\n";
        return 1;
    }

    std::cout << "NovelAgent v0.2.0 | " << provider->name
              << " | " << provider->model << "\n";

    // 打开/创建项目
    ProjectManager pm;
    Project project;
    if (!projectPath.empty()) {
        project = pm.openOrCreate(projectPath);
        if (project.title.empty()) {
            std::cerr << "错误: 无法打开/创建项目 " << projectPath << "\n";
            return 1;
        }
        std::cout << "\n项目: " << project.title
                  << " | 章节: " << project.outline.chapters.size()
                  << " | 角色: " << project.characters.size() << "\n";
    }

    // 创建应用（封装所有组件装配）
    NovelAgentApp novelAgent(*provider, std::move(project));

    // 分发到 REPL 或 --exec
    if (!execCommand.empty()) {
        novelAgent.runExec(execCommand);
    } else if (!projectPath.empty()) {
        novelAgent.runRepl();
    } else {
        std::cout << "使用 -p <目录> 指定项目后进入 REPL。\n";
    }

    return 0;
}
