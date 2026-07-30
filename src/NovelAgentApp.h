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
#include "agent/memory/LongTermMemoryStore.h"
#include "agent/skill/SkillRegistry.h"

struct Project;
namespace agent { class ProjectIndexService; class IIndexService; }
#include <memory>
#include <string>
#include <vector>

// NovelAgent 应用层组装器 — 门面模式封装全部组件装配（纯布线，不含业务逻辑）。
class NovelAgentApp {
public:
    // 装配全部组件：初始化 LLM 客户端、记忆、持久化、技能注册表与内置工具，
    // 并恢复上次会话。disabledTools 指定本次禁用的内置工具名。
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  std::vector<std::string> disabledTools = {});
    ~NovelAgentApp();

    agent::Agent& agent() { return agent_; }                           // 核心对话代理
    agent::ToolRegistry& registry() { return registry_; }              // 工具注册表
    skill::SkillRegistry& skillRegistry() { return skill_registry_; }  // 技能注册表
    std::shared_ptr<Project> project() { return project_; }            // 当前项目（可能为空项目）
    agent::IIndexService* indexService();                              // 项目索引服务；项目未打开时仍返回有效指针

    // 启用/禁用技能：更新注册表 + 持久化 + 重建 system prompt。
    // 返回技能是否存在。低频操作，KV cache 失效可接受。
    bool setSkillEnabled(const std::string& name, bool enabled);

private:
    llm::LLMClientFactory client_;                  // LLM 客户端工厂，持有 Provider 配置
    agent::ToolRegistry registry_;                  // 工具注册表（内置工具登记处）
    llm::Memory memory_;                            // 对话短期记忆
    agent::Agent agent_;                            // 核心对话代理
    std::shared_ptr<Project> project_;              // 当前项目（未打开时为空项目）
    llm::TokenCounter calibrator_;                  // Token 计量/校准器
    FileStorageBackend storage_;                    // 文件存储后端（绑定项目路径）
    agent::SessionPersistence persistence_;         // 会话持久化（基于 storage_）
    retrieval::VectorStore vector_store_;           // 向量存储（检索用）
    retrieval::EmbeddingGenerator embedding_gen_;   // 嵌入向量生成器
    agent::LongTermMemoryStore ltm_store_;          // 长期记忆日志
    std::unique_ptr<agent::ProjectIndexService> index_service_;  // 项目索引服务
    skill::SkillRegistry skill_registry_;           // 技能注册表

    // 装配入口与分段辅助函数（定义在 AppAssembly.cpp，按功能段拆分，调用顺序即装配顺序）。
    void setupAgent(const std::vector<std::string>& disabledTools);
    void setupLongTermMemoryAndSkills();
    void registerBuiltInTools(const std::vector<std::string>& disabledTools);
    void setupSystemPrompt();
    void setupContextAndTokenBudget();
    void setupPersistenceAndVectorStore();
    void setupSummarySinkAndIndexService();
    // 组装 system prompt（人格 + 工具指引 + 技能上下文 + 延迟工具存根）。
    std::string buildSystemPrompt() const;
    // 技能禁用列表持久化（<项目>/.novelagent/skills.json）。
    std::string skillSettingsPath() const;
    // 从 skills.json 读取禁用技能名列表；文件缺失或损坏时返回空列表。
    std::vector<std::string> loadDisabledSkills() const;
    // 将当前禁用技能名列表写入 skills.json。
    void saveDisabledSkills() const;
};
