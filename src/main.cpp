// NovelAgent 程序入口。
// 当前支持两种运行模式：
//   1. 单次命令：novelagent -p my-novel -e "写第三章"
//   2. 交互式 REPL：novelagent -p my-novel
//
// API Key 的优先级为：环境变量 > config.json > 内置默认值。
// 让环境变量优先，便于接入 CI 或 dotenv 工作流。

#include "config/AppConfig.h"
#include "cli/ReplHandler.h"
#include "project/ProjectManager.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};

    std::string projectPath;
    std::string execCommand;
    std::string providerName = "deepseek";
    bool verbose = false;

    app.add_option("-p,--project", projectPath, "项目目录路径");
    app.add_option("-e,--exec", execCommand, "执行单次命令后退出");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "启用调试日志");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (verbose) {
        spdlog::set_level(spdlog::level::debug);
    }

    // 从 ~/.novelagent/config.json 加载配置，缺失时返回空配置。
    AppConfig config = AppConfig::load();

    // 用环境变量覆盖配置文件中的 API Key，
    // 这样用户就不必把密钥明文写进磁盘。
    for (const auto& envProvider : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
        std::string envName = std::string(envProvider) + "_API_KEY";
        const char* envKey = std::getenv(envName.c_str());
        if (envKey) {
            std::string lower = envProvider;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            config.setApiKey(lower, envKey);
        }
    }

    std::cout << "NovelAgent v0.1.0\n";

    // 显示当前默认 provider 信息。
    if (config.getDefaultProvider()) {
        const auto* p = config.getDefaultProvider();
        std::cout << "Provider: " << config.default_provider
                  << " | Model: " << p->model << "\n";
    } else {
        std::cout << "尚未配置 LLM Provider。可通过环境变量设置：\n";
        std::cout << "  DEEPSEEK_API_KEY, KIMI_API_KEY, CLAUDE_API_KEY\n";
    }

    // 按需打开或创建项目。
    ProjectManager pm;
    Project project;

    if (!projectPath.empty()) {
        project = pm.openOrCreate(projectPath);

        if (project.title.empty()) {
            std::cerr << "错误：无法打开或创建项目 " << projectPath << "\n";
            return 1;
        }

        // 显示项目摘要，方便进入 REPL 前快速确认状态。
        std::cout << "\n项目: " << project.title << "\n";
        std::cout << "状态: " << project.status
                  << " | 字数: " << project.current_word_count
                  << "/" << project.target_word_count << "\n";
        std::cout << "章节: " << project.outline.chapters.size()
                  << " | 角色: " << project.characters.size() << "\n\n";
    }

    // 单次命令模式。
    if (!execCommand.empty()) {
        std::cout << "Exec 模式: " << execCommand << "（Phase 3 实现）\n";
        return 0;
    }

    // 交互式 REPL 模式。
    if (!projectPath.empty()) {
        std::cout << "输入 /help 查看可用命令。\n";
        // ReplHandler(project).run();  // Phase 3 实现
    } else {
        std::cout << "使用 -p <目录> 指定项目后再进入 REPL。\n";
    }

    return 0;
}
