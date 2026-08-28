#include "NovelAgentApp.h"

#include "agent/index/ProjectIndexService.h"
#include "agent/prompt/Prompts.h"
#include "project/Models/Project.h"
#include "utils/FileUtils.h"

#include <nlohmann/json.hpp>

namespace {

// 嵌入生成器的基础配置选择：启用了嵌入专用服务时使用它（api_key/base_url/model），
// 否则回退对话 provider（兼容旧行为：嵌入与对话同服务商）。
ProviderConfig embeddingProvider(const ProviderConfig& fallback,
                                 const EmbeddingSettings& es) {
    if (!es.enabled()) return fallback;
    // 显式列全字段避免 -Wmissing-field-initializers（聚合初始化漏字段告警）
    return ProviderConfig{/*name*/ "", /*api_key*/ es.api_key,
                          /*base_url*/ es.base_url, /*model*/ es.model,
                          /*max_context_tokens*/ 1000000, /*temperature*/ 0.7,
                          /*max_tokens*/ 393216, /*supports_cache_control*/ false,
                          /*enable_thinking*/ false, /*reasoning_effort*/ "high"};
}

// 嵌入请求配置：启用嵌入专用服务时按设置构造（端点/协议开关/批量上限），
// 否则默认 OpenAI 兼容。
retrieval::EmbeddingConfig embeddingConfig(const EmbeddingSettings& es) {
    if (!es.enabled()) return {};
    return retrieval::EmbeddingConfig{es.model, es.max_batch_size, 8000,
                                      es.endpoint, es.dashscope_style};
}

} // namespace

NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                             std::shared_ptr<Project> project,
                             const EmbeddingSettings& embedding)
    : client_(provider)
    , agent_(client_, registry_)
    , project_(project ? std::move(project) : std::make_shared<Project>())
    , project_access_(std::make_shared<ProjectAccess>(project_))
    , storage_(project_access_ ? project_access_->path() : "")
    , embedding_gen_(embeddingProvider(provider, embedding),
                     embeddingConfig(embedding))
    , rules_provider_(utils::file::configDir())
{
    setupAgent();
}

NovelAgentApp::~NovelAgentApp()
{
    // SQLite 单库生命周期收尾（未 open 时为 no-op）
    sqlite_store_.close();
}

agent::IIndexService* NovelAgentApp::indexService()
{
    return index_service_.get();
}

std::string NovelAgentApp::buildSystemPrompt()
{
    std::string system_prompt = agent::prompt::kMainPersonality;

    if (project_access_ && !project_access_->path().empty()) {
        system_prompt += "\n\n";
        system_prompt += agent::prompt::kToolUseInstructions;

        // 规则层：全局 + 项目规则叠加（会话边界重读盘，改文件下个会话即生效）
        std::string rules = rules_provider_.combined(project_access_->path());
        if (!rules.empty())
            system_prompt += "\n\n" + rules;

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
    return project_access_->path() + "/.novelagent/skills.json";
}

// 从 <项目>/.novelagent/skills.json 读取被禁用的技能名列表。
// 任何异常路径（无项目、文件缺失、JSON 损坏）都安全地返回空列表。
std::vector<std::string> NovelAgentApp::loadDisabledSkills() const
{
    std::vector<std::string> disabled;
    if (!project_access_ || project_access_->path().empty())
        return disabled;  // 项目未打开：无持久化路径，直接返回空

    const std::string text = utils::file::readText(skillSettingsPath());
    if (text.empty())
        return disabled;  // 文件不存在或为空：尚无禁用记录

    try {
        auto j = nlohmann::json::parse(text);
        // 读取 "disabled_skills" 数组；字段缺失时默认空数组
        for (const auto& name : j.value("disabled_skills", std::vector<std::string>{}))
            disabled.push_back(name);
    } catch (...) {
        // 损坏的配置视为无禁用项
    }
    return disabled;
}

void NovelAgentApp::saveDisabledSkills() const
{
    if (!project_access_ || project_access_->path().empty())
        return;
    nlohmann::json j;
    j["disabled_skills"] = skill_registry_.disabledSkills();
    utils::file::writeText(skillSettingsPath(), j.dump(2));
}
