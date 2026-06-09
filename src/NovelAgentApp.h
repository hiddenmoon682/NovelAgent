#pragma once

#include "agent/Agent.h"
#include "agent/ContextManager.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"
#include "config/AppConfig.h"
#include "llm/LLMClient.h"
#include "project/Models.h"

#include "cli/IOutputChannel.h"
#include <memory>
#include <string>
#include <vector>

class NovelAgentApp {
public:
    /// @param provider  LLM Provider 配置
    /// @param project   已打开的小说项目
    /// @param out       输出通道（默认=控制台）
    /// @param disabledTools  禁用的工具名列表（空=全部启用）
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  IOutputChannel* out = nullptr,
                  std::vector<std::string> disabledTools = {});

    void runRepl(const std::string& welcomeMessage = "");
    void runExec(const std::string& command);

    agent::Agent& agent() { return agent_; }
    agent::ToolRegistry& registry() { return registry_; }
    agent::TemplateManager& templateManager() { return template_mgr_; }
    std::shared_ptr<Project> project() { return project_; }

private:
    std::unique_ptr<IOutputChannel> ownedOutput_;
    IOutputChannel& out_;
    llm::LLMClient client_;
    agent::ToolRegistry registry_;
    agent::Agent agent_;
    agent::ContextManager cm_;
    std::shared_ptr<Project> project_;
    agent::TemplateManager template_mgr_;

    void setupAgent(const std::vector<std::string>& disabledTools);
    void saveConversationIfNeeded(const llm::LLMResponse& response);
};
