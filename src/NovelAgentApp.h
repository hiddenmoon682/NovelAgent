#pragma once

#include "agent/core/Agent.h"
#include "agent/tool/ToolRegistry.h"
#include "config/AppConfig.h"
#include "agent/context/Memory.h"
#include "agent/session/SessionPersistence.h"
#include "llm/LLMClientFactory.h"
#include "llm/TokenCounter.h"
#include "project/FileStorageBackend.h"
#include "retrieval/VectorStore.h"
#include "retrieval/EmbeddingGenerator.h"
#include "agent/skill/SkillRegistry.h"

struct Project;
namespace agent { class ProjectIndexService; }
#include <memory>
#include <string>
#include <vector>

// NovelAgent 应用层组装器 — 门面模式封装全部组件装配（纯布线，不含业务逻辑）。
class NovelAgentApp {
public:
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  std::vector<std::string> disabledTools = {});
    ~NovelAgentApp();

    agent::Agent& agent() { return agent_; }
    agent::ToolRegistry& registry() { return registry_; }
    skill::SkillRegistry& skillRegistry() { return skill_registry_; }
    std::shared_ptr<Project> project() { return project_; }

private:
    llm::LLMClientFactory client_;
    agent::ToolRegistry registry_;
    llm::Memory memory_;
    agent::Agent agent_;
    std::shared_ptr<Project> project_;
    llm::TokenCounter calibrator_;
    FileStorageBackend storage_;
    agent::SessionPersistence persistence_;
    retrieval::VectorStore vector_store_;
    retrieval::EmbeddingGenerator embedding_gen_;
    std::unique_ptr<agent::ProjectIndexService> index_service_;
    skill::SkillRegistry skill_registry_;

    void setupAgent(const std::vector<std::string>& disabledTools);
};
