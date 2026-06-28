# 模块化源文件列表 — 修复 #1。
# 不使用函数包装（PARENT_SCOPE 在 include() 上下文中行为不一致），
# 直接设置变量。

set(NOVELAGENT_CONFIG
    src/config/AppConfig.h src/config/AppConfig.cpp
)

set(NOVELAGENT_UTILS
    src/utils/FileUtils.h src/utils/FileUtils.cpp
    src/utils/StringUtils.h src/utils/JsonUtils.h src/utils/SchemaUtils.h
)

set(NOVELAGENT_PROJECT
    src/project/Models.h
    src/project/Models/ModelDetail.h
    src/project/Models/ModelsFwd.h
    src/project/Models/Scene.h
    src/project/Models/Relationship.h
    src/project/Models/CharacterDevelopment.h
    src/project/Models/Character.h
    src/project/Models/Setting.h
    src/project/Models/WorldRule.h
    src/project/Models/PlotThread.h
    src/project/Models/Volume.h
    src/project/Models/Chapter.h
    src/project/Models/Style.h
    src/project/Models/Outline.h
    src/project/Models/Project.h
    src/project/IProjectAccess.h src/project/ProjectAccess.h
    src/project/IStorageBackend.h
    src/project/FileStorageBackend.h src/project/FileStorageBackend.cpp
    src/project/ProjectIO.h src/project/ProjectIO.cpp
    src/project/ProjectManager.h src/project/ProjectManager.cpp
)

set(NOVELAGENT_LLM
    src/llm/Message.h src/llm/ILLMClient.h src/llm/StreamingTypes.h
    src/llm/TokenCounter.h src/llm/TokenCounter.cpp
    src/llm/SSEParser.h src/llm/SSEParser.cpp
    src/llm/StreamAccumulator.h src/llm/StreamAccumulator.cpp
    src/llm/StreamingPipeline.h
    src/llm/HttpClient.h src/llm/HttpClient.cpp
    src/llm/LLMClient.h src/llm/LLMClient.cpp
    src/llm/LLMClientFactory.h src/llm/LLMClientFactory.cpp
    src/llm/Conversation.h
)

set(NOVELAGENT_RETRIEVAL
    src/retrieval/VectorStore.h src/retrieval/VectorStore.cpp
    src/retrieval/IVectorStore.h
    src/retrieval/EmbeddingGenerator.h src/retrieval/EmbeddingGenerator.cpp
    src/retrieval/IEmbeddingGenerator.h
    src/retrieval/NovelChunker.h src/retrieval/NovelChunker.cpp
)

set(NOVELAGENT_AGENT
    src/agent/ToolRegistry.h src/agent/ToolRegistry.cpp
    src/agent/IToolProvider.h src/agent/IToolProvider.cpp
    src/agent/AgentOrchestratorTypes.h src/agent/ContextManagerTypes.h
    src/agent/AgentState.h src/agent/AgentState.cpp
    src/agent/ParameterValidator.h src/agent/ParameterValidator.cpp
    src/agent/ExecutionTracer.h src/agent/ExecutionTracer.cpp
    src/agent/ToolCallLoop.h src/agent/ToolCallLoop.cpp
    src/agent/IMessageProcessor.h src/agent/IMessageProcessor.cpp
    src/agent/ISynthesisStrategy.h src/agent/ISynthesisStrategy.cpp
    src/agent/SessionPersistence.h src/agent/SessionPersistence.cpp
    src/agent/ContextManager.h src/agent/ContextManager.cpp
    src/agent/PromptComposer.h
    src/agent/PromptContextBuilder.h src/agent/PromptContextBuilder.cpp
    src/agent/Agent.h src/agent/Agent.cpp
    src/agent/ToolPipeline.h src/agent/ToolPipeline.cpp
    src/agent/SubAgent.h src/agent/SubAgent.cpp
    src/agent/AgentOrchestrator.h src/agent/AgentOrchestrator.cpp
    src/agent/SubAgentTemplate.h
    src/agent/TemplateManager.h src/agent/TemplateManager.cpp
    src/agent/tools/BuiltInTool.h src/agent/tools/BuiltInTool.cpp
)

set(NOVELAGENT_TOOLS
    src/agent/tools/ChapterTools.h src/agent/tools/ChapterTools.cpp
    src/agent/tools/CharacterTools.h src/agent/tools/CharacterTools.cpp
    src/agent/tools/SettingTools.h src/agent/tools/SettingTools.cpp
    src/agent/tools/WorldRuleTools.h src/agent/tools/WorldRuleTools.cpp
    src/agent/tools/OutlineTools.h src/agent/tools/OutlineTools.cpp
    src/agent/tools/ShellTools.h src/agent/tools/ShellTools.cpp
    src/agent/tools/SearchMemoryTools.h src/agent/tools/SearchMemoryTools.cpp
    src/agent/tools/StyleTools.h src/agent/tools/StyleTools.cpp
    src/agent/AgentSetup.h src/agent/AgentSetup.cpp
)

set(NOVELAGENT_CLI
    src/cli/AnsiTerminal.h
    src/cli/IOutputChannel.h src/cli/ConsoleOutput.h
    src/cli/TerminalGUI.h src/cli/TerminalGUI.cpp
    src/cli/CommandParser.h src/cli/CommandParser.cpp
    src/cli/StreamDisplay.h src/cli/StreamDisplay.cpp
    src/cli/ReplHandler.h src/cli/ReplHandler.cpp
)

set(NOVELAGENT_SERVER
    src/server/SSEQueue.h
    src/server/SessionManager.h src/server/SessionManager.cpp
    src/server/BackendServer.h src/server/BackendServer.cpp
)

# TUI 模块已移除（Tauri 前端替代）
