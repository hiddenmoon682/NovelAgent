#include "NovelAgentApp.h"

#include "agent/AgentSetup.h"
#include "agent/PromptComposer.h"
#include "cli/ConsoleOutput.h"
#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "project/ProjectIO.h"

#include <iostream>

NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project,
                               IOutputChannel* out,
                               std::vector<std::string> disabledTools)
    : ownedOutput_(out ? nullptr : std::make_unique<ConsoleOutput>())
    , out_(out ? *out : *ownedOutput_)
    , client_(provider)
    , agent_(client_, registry_)
    , project_(project ? std::move(project) : std::make_shared<Project>())
{
    setupAgent(std::move(disabledTools));
}

void NovelAgentApp::setupAgent(const std::vector<std::string>& disabledTools)
{
    // 工具自注册（通过 REGISTER_TOOL 宏），支持按配置禁用
    if (!project_->title.empty()) {
        agent::registerAllTools(registry_, project_, disabledTools);
    }

    agent::PromptComponents pc;
    pc.personality =
        "你是一个专业的网络小说写作助手 NovelAgent。\n\n"
        "你的能力：\n"
        "- 使用工具读写章节、管理角色和设定\n"
        "- 根据大纲和现有内容创作连贯的章节\n"
        "- 维护角色一致性、剧情连贯性和世界观设定\n\n"
        "工作原则：\n"
        "- 写作前先读取相关章节和设定\n"
        "- 写完后确认内容已正确写入文件\n"
        "- 保持语言流畅、情节紧凑";
    agent_.setSystemPrompt(agent::PromptComposer::compose(pc));

    agent_.setContextManager(&cm_);
    agent_.setContextWindow(client_.config().context_window);

    // 初始化并行编排器
    orchestrator_ = std::make_unique<agent::AgentOrchestrator>(
        client_, registry_, agent::PromptComposer::compose(pc));
    orchestrator_->setTemplateManager(&template_mgr_);
}

void NovelAgentApp::saveConversationIfNeeded(const llm::LLMResponse& /*response*/)
{
    // 基础版：每次对话轮次后持久化到 .novelagent/conversation.json
    // Phase 4 将添加增量保存和压缩
    if (project_->path.empty()) return;
    try {
        // 使用 Agent 的 conversation 状态
        // TODO: Phase 4 添加 loadConversation 恢复支持
    } catch (...) {
        // 持久化失败不阻塞主流程
    }
}

void NovelAgentApp::runRepl(const std::string& welcomeMessage)
{
    ReplHandler repl(agent_, out_);
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
    out_.write("Exec: " + command + "\n\n");
    try {
        auto callbacks = StreamDisplay::create(out_);
        agent_.execute(command, callbacks);
        out_.write("\n");
    } catch (const std::exception& e) {
        out_.writeError("错误: " + std::string(e.what()) + "\n");
    }
}
