#include "NovelAgentApp.h"

#include "agent/index/ProjectIndexService.h"
#include "agent/prompt/Prompts.h"
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
