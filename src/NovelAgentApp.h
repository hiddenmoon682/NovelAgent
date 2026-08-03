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
#include "agent/prompt/RulesProvider.h"
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
    // 并恢复上次会话。内置工具禁用列表由工具配置（tools.json）驱动，
    // 不再由调用方传入。
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project);
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
    agent::prompt::RulesProvider rules_provider_;   // 规则层（全局 + 项目规则叠加）

    // 装配入口：依序调用各分段辅助函数完成 Agent 全部初始化（无外部参数）。
    void setupAgent();

    // 初始化长期记忆日志与技能系统（含默认规则落盘、搜索路径与禁用列表恢复）。
    // 必须在 registerBuiltInTools 之前调用：save_memory 依赖已初始化的 store，
    // use_skill/save_skill 依赖已就绪的 registry 指针。
    void setupLongTermMemoryAndSkills();

    // 将内置工具注册到工具注册表（仅项目有效时执行）。
    // 依赖包以指针传入，工具借此访问向量库/记忆/技能。
    void registerBuiltInTools();

    // 设置系统 prompt 并注册 prompt 重建 provider。
    // 会话边界（新建/切换）时重建 prompt，使 save_skill 新增的技能目录对 LLM 可见；
    // 会话中途不重建以保持 KV cache 稳定。
    void setupSystemPrompt();

    // 注入上下文管理组件（项目指针、Token 校准器）并设置 Token 预算。
    // 预算需先于持久化注入：启动恢复时的用量百分比需用真实模型上限计算。
    void setupContextAndTokenBudget();

    // 启用会话持久化并初始化向量存储（均仅项目已打开时执行，
    // 避免空路径时写到盘符根目录 /.novelagent）。
    void setupPersistenceAndVectorStore();

    // 将会话压缩摘要沉淀到长期记忆日志，并创建项目索引服务。
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
