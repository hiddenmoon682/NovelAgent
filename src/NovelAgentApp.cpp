#include "NovelAgentApp.h"

#include "agent/tools/BuiltInTool.h"
#include "agent/index/ProjectIndexService.h"
#include "agent/prompt/Prompts.h"
#include "project/Models/Project.h"
#include "utils/FileUtils.h"

NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project,
                               std::vector<std::string> disabledTools)
    : client_(provider)
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

    // 延迟工具存根（静态）一次性注入，避免运行时拼接破坏 KV cache
    system_prompt += agent_.deferredToolsStub();

    agent_.setSystemPrompt(std::move(system_prompt));

    // 注入上下文管理组件
    agent_.setProject(project_.get());
    agent_.setCalibrator(&calibrator_);

    // Token 预算先于持久化注入：启动恢复时的用量百分比需用真实模型上限计算
    agent::TokenBudget budget;
    budget.model_limit = client_.config().max_context_tokens;
    agent_.setTokenBudget(budget);

    // 仅项目已打开时启用持久化（避免空路径时写到盘符根目录 /.novelagent）
    if (project_ && !project_->path.empty()) {
        agent_.setPersistence(&persistence_);
        agent_.loadSessionState();  // 启动时恢复 active 会话（system prompt 以本次装配为准）
    }

    if (project_ && !project_->path.empty()) {
        std::string vec_path = project_->path + "/.novelagent/vectors.json";
        vector_store_.init(vec_path);
    }

    index_service_ = std::make_unique<agent::ProjectIndexService>(
        project_, vector_store_, embedding_gen_);
}
