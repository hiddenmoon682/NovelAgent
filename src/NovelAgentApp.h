#pragma once

#include "agent/Agent.h"
#include "agent/ContextManager.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"
#include "config/AppConfig.h"
#include "llm/Conversation.h"
#include "llm/LLMClientFactory.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "retrieval/VectorStore.h"
#include "retrieval/EmbeddingGenerator.h"
#include "skill/SkillRegistry.h"

#include "cli/IOutputChannel.h"

struct Project;
namespace agent { class ProjectIndexService; }
#include <memory>
#include <string>
#include <vector>

// NovelAgent 应用层组装器 — 门面模式封装全部组件装配（纯布线，不含业务逻辑）。
class NovelAgentApp {
public:
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  IOutputChannel* out = nullptr,
                  std::vector<std::string> disabledTools = {});
    ~NovelAgentApp();

    void runRepl(const std::string& welcomeMessage = "");
    void runExec(const std::string& command);

    agent::Agent& agent() { return agent_; }
    agent::ContextManager& contextManager() { return cm_; }
    agent::ToolRegistry& registry() { return registry_; }
    agent::TemplateManager& templateManager() { return template_mgr_; }
    skill::SkillRegistry& skillRegistry() { return skill_registry_; }
    std::shared_ptr<Project> project() { return project_; }

private:
    std::unique_ptr<IOutputChannel> ownedOutput_;
    IOutputChannel& out_;
    llm::LLMClientFactory client_;
    agent::ToolRegistry registry_;
    llm::Memory memory_;                              //  记忆（对等组件，注入 Agent）
    agent::Agent agent_;
    std::shared_ptr<Project> project_;
    llm::TokenCounter calibrator_;
    FileStorageBackend storage_;
    agent::ContextManager cm_;
    retrieval::VectorStore vector_store_;
    retrieval::EmbeddingGenerator embedding_gen_;
    agent::TemplateManager template_mgr_;
    std::unique_ptr<agent::ProjectIndexService> index_service_;
    skill::SkillRegistry skill_registry_;

    void setupAgent(const std::vector<std::string>& disabledTools);
};
