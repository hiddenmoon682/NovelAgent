#pragma once

#include "agent/Agent.h"
#include "agent/ContextManager.h"
#include "agent/ToolRegistry.h"
#include "config/AppConfig.h"
#include "llm/LLMClient.h"
#include "project/Models.h"

#include <string>

/// NovelAgent 应用程序门面 — 封装所有组件的创建、装配和生命周期管理。
///
/// 替代 main.cpp 中 ~30 行手动装配代码，提供清晰的初始化流程。
///
/// 使用示例:
///   NovelAgentApp app(providerConfig, project);
///   app.runRepl();    // 交互式 REPL
///   // 或
///   app.runExec("写第三章");  // 单次命令
class NovelAgentApp {
public:
    /// @param provider  已加载的 LLM Provider 配置
    /// @param project   已打开/创建的小说项目（可为空 Project）
    NovelAgentApp(const ProviderConfig& provider, Project project);

    /// 启动交互式 REPL 循环。
    void runRepl(const std::string& welcomeMessage = "");

    /// 执行单次命令（--exec 模式）。
    void runExec(const std::string& command);

    // ── 访问子组件（用于测试和扩展）──

    agent::Agent& agent() { return agent_; }
    agent::ToolRegistry& registry() { return registry_; }
    Project& project() { return project_; }

private:
    llm::LLMClient client_;
    agent::ToolRegistry registry_;
    agent::Agent agent_;
    agent::ContextManager cm_;
    Project project_;
    bool toolsRegistered_ = false;

    void setupAgent();
};
