#pragma once

#include "agent/Agent.h"
#include "agent/ContextManager.h"
#include "agent/IIndexService.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"
#include "config/AppConfig.h"
#include "llm/LLMClientFactory.h"
#include "project/FileStorageBackend.h"
#include "retrieval/VectorStore.h"
#include "retrieval/EmbeddingGenerator.h"

#include "cli/IOutputChannel.h"

struct Project;
#include <memory>
#include <string>
#include <vector>

// NovelAgent 应用层组装器 — 门面模式封装全部组件装配。
// Issue 6: 实现 IIndexService，ReplHandler 通过抽象接口访问索引功能，
// 消除 ReplHandler → NovelAgentApp* 的反向依赖。
class NovelAgentApp : public agent::IIndexService {
public:
    // @param provider  LLM Provider 配置
    // @param project   已打开的小说项目
    // @param out       输出通道（默认=控制台）
    // @param disabledTools  禁用的工具名列表（空=全部启用）
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  IOutputChannel* out = nullptr,
                  std::vector<std::string> disabledTools = {});

    void runRepl(const std::string& welcomeMessage = "");
    void runExec(const std::string& command);

    agent::Agent& agent() { return agent_; }
    agent::ContextManager& contextManager() { return cm_; }
    agent::ToolRegistry& registry() { return registry_; }
    agent::TemplateManager& templateManager() { return template_mgr_; }
    std::shared_ptr<Project> project() { return project_; }

    // Issue 6: IIndexService 实现 — 替代原来的 vectorStore()/embeddingGenerator()
    agent::IndexResult indexAll(std::function<void(const std::string&)> progress = nullptr) override;

private:
    std::unique_ptr<IOutputChannel> ownedOutput_;
    IOutputChannel& out_;
    llm::LLMClientFactory client_;  // LLMClient 工厂（构造后不可变，传递给 Agent 创建独立客户端）
    agent::ToolRegistry registry_;
    agent::Agent agent_;
    std::shared_ptr<Project> project_;
    FileStorageBackend storage_;   // 必须在 cm_ 之前初始化
    agent::ContextManager cm_;
    retrieval::VectorStore vector_store_;               // 向量存储（语义检索）
    retrieval::EmbeddingGenerator embedding_gen_;       // 嵌入生成器
    agent::TemplateManager template_mgr_;

    void setupAgent(const std::vector<std::string>& disabledTools);
};
