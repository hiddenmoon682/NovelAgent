#include "NovelAgentApp.h"

#include "agent/tools/BuiltInTool.h"
#include "agent/index/ProjectIndexService.h"
#include "agent/prompt/Prompts.h"
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
    , persistence_(storage_)
    , embedding_gen_(provider)
{
    setupAgent(std::move(disabledTools));
}

NovelAgentApp::~NovelAgentApp() = default;

void NovelAgentApp::setupAgent(const std::vector<std::string>& disabledTools)
{
    if (project_ && !project_->title.empty()) {
        agent::ToolDependencies deps{project_, &vector_store_, &embedding_gen_};
        agent::BuiltInTool::registerAllTo(registry_, deps, disabledTools);
    }

    std::string system_prompt = agent::prompt::kMainPersonality;

    if (project_ && !project_->path.empty()) {
        system_prompt += "\n\n";
        system_prompt += agent::prompt::kToolUseInstructions;

        skill_registry_.addSearchPath(project_->path + "/skills");
        skill_registry_.addSearchPath(utils::file::homeDir() + "/.novelagent/skills");
        skill_registry_.discoverAll();
        std::string skill_ctx = skill_registry_.getSkillContext();
        if (!skill_ctx.empty())
            system_prompt += "\n\n## 可用技能\n" + skill_ctx;
    }

    agent_.setSystemPrompt(std::move(system_prompt));

    // 注入上下文管理组件
    agent_.setProject(project_.get());
    agent_.setCalibrator(&calibrator_);
    agent_.setPersistence(&persistence_);

    agent::TokenBudget budget;
    budget.model_limit = client_.config().max_context_tokens;
    agent_.setTokenBudget(budget);

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
