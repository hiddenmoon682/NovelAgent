#include "NovelAgentApp.h"

#include "agent/tools/BuiltInTool.h"
#include "agent/index/ProjectIndexService.h"
#include "agent/prompt/Prompts.h"
#include "agent/skill/BuiltinSkills.h"
#include "project/Models/Project.h"
#include "utils/FileUtils.h"

#include <nlohmann/json.hpp>

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

agent::IIndexService* NovelAgentApp::indexService()
{
    return index_service_.get();
}

void NovelAgentApp::setupAgent(const std::vector<std::string>& disabledTools)
{
    // 长期记忆日志与技能发现均须先于工具注册完成
    //（save_memory 依赖已初始化的 store；use_skill/save_skill 持有 registry 指针）
    if (project_ && !project_->path.empty()) {
        ltm_store_.init(project_->path + "/.novelagent/memories.json");

        const std::string global_skills = utils::file::configDir() + "/skills";
        skill::installBuiltinSkills(global_skills);

        skill_registry_.addSearchPath(project_->path + "/skills");
        skill_registry_.addSearchPath(global_skills);
        skill_registry_.setDisabledSkills(loadDisabledSkills());
        skill_registry_.discoverAll();
    }

    if (project_ && !project_->title.empty()) {
        agent::ToolDependencies deps{project_, &vector_store_, &embedding_gen_,
                                     &ltm_store_, &skill_registry_};
        agent::BuiltInTool::registerAllTo(registry_, deps, disabledTools);
    }

    agent_.setSystemPrompt(buildSystemPrompt());
    // 会话边界（新建/切换）时重建 prompt：save_skill 新增的技能目录
    // 才能在下个会话对 LLM 可见（会话中途不重建，保 KV cache 稳定）
    agent_.setSystemPromptProvider([this] { return buildSystemPrompt(); });

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

    // 会话压缩摘要自动沉淀到长期记忆日志
    agent_.setSummarySink([this](const std::string& summary) {
        if (ltm_store_.initialized())
            ltm_store_.append(summary, "summary");
    });

    index_service_ = std::make_unique<agent::ProjectIndexService>(
        project_, vector_store_, embedding_gen_, &ltm_store_);
}

std::string NovelAgentApp::buildSystemPrompt() const
{
    std::string system_prompt = agent::prompt::kMainPersonality;

    if (project_ && !project_->path.empty()) {
        system_prompt += "\n\n";
        system_prompt += agent::prompt::kToolUseInstructions;

        // 渐进式技能上下文：常驻技能全文 + 按需技能目录（use_skill 加载）
        std::string skill_ctx = skill_registry_.getSkillContext();
        if (!skill_ctx.empty())
            system_prompt += "\n\n## 可用技能\n" + skill_ctx;
    }

    // 延迟工具存根（静态）一次性注入，避免运行时拼接破坏 KV cache
    system_prompt += agent_.deferredToolsStub();
    return system_prompt;
}

bool NovelAgentApp::setSkillEnabled(const std::string& name, bool enabled)
{
    if (!skill_registry_.setEnabled(name, enabled))
        return false;
    saveDisabledSkills();
    agent_.setSystemPrompt(buildSystemPrompt());
    return true;
}

std::string NovelAgentApp::skillSettingsPath() const
{
    return project_->path + "/.novelagent/skills.json";
}

std::vector<std::string> NovelAgentApp::loadDisabledSkills() const
{
    std::vector<std::string> disabled;
    if (!project_ || project_->path.empty())
        return disabled;

    const std::string text = utils::file::readText(skillSettingsPath());
    if (text.empty())
        return disabled;

    try {
        auto j = nlohmann::json::parse(text);
        for (const auto& name : j.value("disabled_skills", std::vector<std::string>{}))
            disabled.push_back(name);
    } catch (...) {
        // 损坏的配置视为无禁用项
    }
    return disabled;
}

void NovelAgentApp::saveDisabledSkills() const
{
    if (!project_ || project_->path.empty())
        return;
    nlohmann::json j;
    j["disabled_skills"] = skill_registry_.disabledSkills();
    utils::file::writeText(skillSettingsPath(), j.dump(2));
}
