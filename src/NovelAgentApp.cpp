#include "NovelAgentApp.h"

#include "agent/AgentSetup.h"
#include "agent/ProjectIndexService.h"
#include "agent/PromptComposer.h"
#include "cli/ConsoleOutput.h"
#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "project/Models/Project.h"
#include "utils/FileUtils.h"

#include <iostream>

NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project,
                               IOutputChannel* out,
                               std::vector<std::string> disabledTools)
    : ownedOutput_(out ? nullptr : std::make_unique<ConsoleOutput>())
    , out_(out ? *out : *ownedOutput_)
    , client_(provider)
    , agent_(client_, registry_, memory_)
    , project_(project ? std::move(project) : std::make_shared<Project>())
    , storage_(project_ ? project_->path : "")
    , cm_(storage_)
    , embedding_gen_(provider)
{
    setupAgent(std::move(disabledTools));
}

NovelAgentApp::~NovelAgentApp() = default;

void NovelAgentApp::setupAgent(const std::vector<std::string>& disabledTools)
{
    if (project_ && !project_->title.empty()) {
        agent::ToolDependencies deps{project_, &vector_store_, &embedding_gen_};
        agent::registerAllTools(registry_, deps, disabledTools);
    }

    agent::PromptComponents pc;
    pc.personality =
        "你是一个专业的网络小说写作助手 NovelAgent。\n\n"
        "你的能力：\n"
        "- 使用工具读写章节、管理角色和设定\n"
        "- 根据大纲和现有内容创作连贯的章节\n"
        "- 维护角色一致性、剧情连贯性和世界观设定\n\n"
        "工作原则：\n"
        "- 【主动获取上下文】使用 get_chapter_context() / get_relevant_characters() 等工具\n"
        "  按需获取本章相关的设定、角色和规则，不要在 system prompt 中等待被动注入\n"
        "- 【按需查询】不要一次性获取所有信息。先了解核心上下文，\n"
        "  写作中需要确认细节时再调用单个查询工具\n"
        "- 写完后确认内容已正确写入文件\n"
        "- 保持语言流畅、情节紧凑";

    // 技能发现与注入
    if (project_ && !project_->path.empty()) {
        skill_registry_.addSearchPath(project_->path + "/skills");
        skill_registry_.addSearchPath(utils::file::homeDir() + "/.novelagent/skills");
        skill_registry_.discoverAll();
        std::string skill_ctx = skill_registry_.getSkillContext();
        if (!skill_ctx.empty())
            pc.context = "## 可用技能\n" + skill_ctx;
    }

    agent_.setSystemPrompt(agent::PromptComposer::compose(pc));

    // 注入 Token 自校准器到 ContextManager（利用 API 返回的真实 token 做 EMA 修正）
    cm_.setCalibrator(&calibrator_);

    agent_.setContextManager(&cm_);
    cm_.setModelContextLimit(client_.config().max_context_tokens);
    cm_.setProject(project_.get());

    // 初始化向量检索后端
    if (project_ && !project_->path.empty()) {
        std::string vec_path = project_->path + "/.novelagent/vectors.json";
        vector_store_.init(vec_path);
    }

    index_service_ = std::make_unique<agent::ProjectIndexService>(
        project_, vector_store_, embedding_gen_);
}

void NovelAgentApp::runRepl(const std::string& welcomeMessage)
{
    ReplHandler repl(agent_, out_, project_);
    repl.setIndexService(index_service_.get());
    repl.setSkillProvider(&skill_registry_);
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
    out_.write("执行: " + command + "\n\n");
    try {
        auto callbacks = StreamDisplay::create(out_);
        agent_.execute(command, callbacks);
        out_.write("\n");
    } catch (const std::exception& e) {
        std::string err = e.what();
        // 友好错误提示
        if (err.find("401") != std::string::npos || err.find("API Key") != std::string::npos)
            out_.writeError("错误: API Key 无效，请检查 config.json 中的密钥配置。\n");
        else if (err.find("Connection") != std::string::npos || err.find("连接") != std::string::npos)
            out_.writeError("错误: 网络连接失败，请检查网络后重试。\n");
        else if (err.find("json.exception") != std::string::npos)
            out_.writeError("错误: API 响应解析失败，请检查 API 密钥和网络连接。\n");
        else
            out_.writeError("错误: " + err + "\n");
    }
}
