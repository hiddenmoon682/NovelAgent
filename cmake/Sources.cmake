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
)

set(NOVELAGENT_RETRIEVAL
    src/retrieval/VectorStore.h src/retrieval/VectorStore.cpp
    src/retrieval/IVectorStore.h
    src/retrieval/EmbeddingGenerator.h src/retrieval/EmbeddingGenerator.cpp
    src/retrieval/IEmbeddingGenerator.h
    src/retrieval/NovelChunker.h src/retrieval/NovelChunker.cpp
)

set(NOVELAGENT_AGENT
    # core/
    src/agent/core/Agent.h src/agent/core/Agent.cpp
    src/agent/core/AgentState.h src/agent/core/AgentState.cpp
    src/agent/core/CoreLoop.h src/agent/core/CoreLoop.cpp
    src/agent/core/ExecutionTracer.h src/agent/core/ExecutionTracer.cpp
    # tool/
    src/agent/tool/IToolProvider.h src/agent/tool/IToolProvider.cpp
    src/agent/tool/ToolRegistry.h src/agent/tool/ToolRegistry.cpp
    src/agent/tool/ToolPipeline.h src/agent/tool/ToolPipeline.cpp
    src/agent/tool/ProgressiveToolProvider.h src/agent/tool/ProgressiveToolProvider.cpp
    src/agent/tool/ParameterValidator.h src/agent/tool/ParameterValidator.cpp
    src/agent/tool/ThreadPool.h
    # context/
    src/agent/context/IMemory.h
    src/agent/context/Memory.h
    src/agent/context/ContextManagerTypes.h
    src/agent/context/TokenBudget.h
    src/agent/context/Compactor.h src/agent/context/Compactor.cpp
    src/agent/context/ContextBudgetEvaluator.h src/agent/context/ContextBudgetEvaluator.cpp
    # prompt/
    src/agent/prompt/Prompts.h src/agent/prompt/Prompts.cpp
    src/agent/prompt/PromptSelector.h src/agent/prompt/PromptSelector.cpp
    src/agent/prompt/PromptContextBuilder.h src/agent/prompt/PromptContextBuilder.cpp
    # session/
    src/agent/session/SessionPersistence.h src/agent/session/SessionPersistence.cpp
    # index/
    src/agent/index/IIndexService.h
    src/agent/index/ProjectIndexService.h src/agent/index/ProjectIndexService.cpp
    # tools/ (基础工具定义)
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
    src/agent/tools/ChapterContextTools.h src/agent/tools/ChapterContextTools.cpp
    src/agent/tools/GetLatestChapterTool.h src/agent/tools/GetLatestChapterTool.cpp
    src/agent/tools/RelevantCharacterTools.h src/agent/tools/RelevantCharacterTools.cpp
    src/agent/tools/RelevantSettingTools.h src/agent/tools/RelevantSettingTools.cpp
    src/agent/tools/RelevantWorldRuleTools.h src/agent/tools/RelevantWorldRuleTools.cpp
)

set(NOVELAGENT_SKILL
    src/agent/skill/SkillMetadata.h
    src/agent/skill/ISkillProvider.h
    src/agent/skill/SkillLoader.h src/agent/skill/SkillLoader.cpp
    src/agent/skill/SkillRegistry.h src/agent/skill/SkillRegistry.cpp
)
