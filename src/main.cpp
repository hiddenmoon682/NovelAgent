// NovelAgent 程序入口。
// 当前支持两种运行模式：
//   1. 单次命令：novelagent -p my-novel -e "写第三章"
//   2. 交互式 REPL：novelagent -p my-novel
//
// API Key 的优先级为：环境变量 > config.json > 内置默认值。

#include "config/AppConfig.h"
#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "agent/Agent.h"
#include "agent/AgentSetup.h"
#include "agent/ContextManager.h"
#include "agent/ToolRegistry.h"
#include "llm/LLMClient.h"
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

    if (verbose) spdlog::set_level(spdlog::level::debug);

    // 加载配置
    AppConfig config = AppConfig::load();

    // 环境变量覆盖
    for (const auto& envProvider : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
        std::string envName = std::string(envProvider) + "_API_KEY";
        const char* envKey = std::getenv(envName.c_str());
        if (envKey) {
            std::string lower = envProvider;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            config.setApiKey(lower, envKey);
        }
    }

    // 获取 provider 配置
    auto* provider = config.getProvider(providerName);
    if (!provider) {
        std::cerr << "错误: 未找到 provider '" << providerName << "' 的配置\n";
        return 1;
    }

    std::cout << "NovelAgent v0.2.0\n";
    std::cout << "Provider: " << provider->name
              << " | Model: " << provider->model << "\n";

    // 打开或创建项目
    ProjectManager pm;
    Project project;

    if (!projectPath.empty()) {
        project = pm.openOrCreate(projectPath);
        if (project.title.empty()) {
            std::cerr << "错误: 无法打开或创建项目 " << projectPath << "\n";
            return 1;
        }

        std::cout << "\n项目: " << project.title << "\n";
        std::cout << "状态: " << project.status
                  << " | 字数: " << project.current_word_count
                  << "/" << project.target_word_count << "\n";
        std::cout << "章节: " << project.outline.chapters.size()
                  << " | 角色: " << project.characters.size() << "\n\n";
    }

    // 初始化核心组件
    llm::LLMClient client(*provider);
    agent::ToolRegistry registry;

    // 如果有项目，注册所有工具
    if (!projectPath.empty()) {
        agent::registerAllTools(registry, project);
    }

    // 创建 Agent
    agent::Agent agent(client, registry);
    agent.setSystemPrompt(
        "你是一个专业的网络小说写作助手 NovelAgent。\n\n"
        "你的能力：\n"
        "- 使用工具读写章节、管理角色和设定\n"
        "- 根据大纲和现有内容创作连贯的章节\n"
        "- 维护角色一致性、剧情连贯性和世界观设定\n\n"
        "工作原则：\n"
        "- 写作前先读取相关章节和设定\n"
        "- 写完后确认内容已正确写入文件\n"
        "- 保持语言流畅、情节紧凑"
    );

    // 设置 ContextManager（启用 token 预算管理）。
    // 注意: cm 生命周期必须 ≥ agent 生命周期（agent 持有裸指针）。
    // 当前在同一作用域内，安全。如果重构为分离函数，需改为 shared_ptr。
    agent::ContextManager cm;
    agent.setContextManager(&cm);
    agent.setContextWindow(provider->context_window);

    // 单次命令模式
    if (!execCommand.empty()) {
        std::cout << "Exec: " << execCommand << "\n\n";
        try {
            auto callbacks = StreamDisplay::create();
            auto response = agent.execute(execCommand, callbacks);
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cerr << "错误: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    // 交互式 REPL 模式
    if (!projectPath.empty()) {
        ReplHandler repl(agent);
        repl.setWelcomeMessage(
            "欢迎使用 NovelAgent！\n"
            "你可以让我帮你写章节、创建角色、管理设定等。"
        );
        repl.run();
    } else {
        std::cout << "使用 -p <目录> 指定项目后进入 REPL。\n";
    }

    return 0;
}
