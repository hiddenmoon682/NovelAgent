#include "NovelAgentApp.h"

#include "agent/AgentSetup.h"
#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"

#include <iostream>

NovelAgentApp::NovelAgentApp(const ProviderConfig& provider, Project project)
    : client_(provider)
    , agent_(client_, registry_)
    , project_(std::move(project))
{
    setupAgent();
}

void NovelAgentApp::setupAgent()
{
    // 注册所有工具（如果有项目）
    if (!project_.title.empty()) {
        agent::registerAllTools(registry_, project_);
        toolsRegistered_ = true;
    }

    // 配置 Agent
    agent_.setSystemPrompt(
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

    agent_.setContextManager(&cm_);
    agent_.setContextWindow(client_.config().context_window);
}

void NovelAgentApp::runRepl(const std::string& welcomeMessage)
{
    ReplHandler repl(agent_);
    if (!welcomeMessage.empty()) {
        repl.setWelcomeMessage(welcomeMessage);
    } else {
        repl.setWelcomeMessage(
            "欢迎使用 NovelAgent！\n"
            "你可以让我帮你写章节、创建角色、管理设定等。"
        );
    }
    repl.run();
}

void NovelAgentApp::runExec(const std::string& command)
{
    std::cout << "Exec: " << command << "\n\n";
    try {
        auto callbacks = StreamDisplay::create();
        agent_.execute(command, callbacks);
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
    }
}
