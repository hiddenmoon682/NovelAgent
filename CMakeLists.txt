cmake_minimum_required(VERSION 3.24)
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(EXISTS "D:/SoftWare/msys2/mingw64")
    list(PREPEND CMAKE_PREFIX_PATH "D:/SoftWare/msys2/mingw64")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerSettings)
include(FetchDependencies)

set(OPENSSL_ROOT_DIR "D:/SoftWare/msys2/mingw64" CACHE PATH "OpenSSL root directory")
if(NOT EXISTS "${OPENSSL_ROOT_DIR}")
    unset(OPENSSL_ROOT_DIR CACHE)
endif()
find_package(OpenSSL REQUIRED)

# ============================================================================
# 分层对象库 — 拆分为 core + tools，减少增量链接
#
# 修改工具代码时，只重链接需要工具的测试（5个），不影响纯core测试。
# 修改 core 时，所有目标仍需重链接（但 core 修改频率远低于 tools）。
# ============================================================================

set(CORE_SOURCES
    src/config/AppConfig.h
    src/config/AppConfig.cpp

    src/utils/FileUtils.h
    src/utils/FileUtils.cpp
    src/utils/StringUtils.h
    src/utils/JsonUtils.h
    src/utils/SchemaUtils.h

    src/project/Models.h
    src/project/IProjectAccess.h
    src/project/ProjectAccess.h
    src/project/IStorageBackend.h
    src/project/FileStorageBackend.h
    src/project/FileStorageBackend.cpp
    src/project/ProjectIO.h
    src/project/ProjectIO.cpp
    src/project/ProjectManager.h
    src/project/ProjectManager.cpp

    src/llm/Message.h
    src/llm/ILLMClient.h
    src/llm/StreamingTypes.h
    src/llm/TokenCounter.h
    src/llm/TokenCounter.cpp
    src/llm/SSEParser.h
    src/llm/SSEParser.cpp
    src/llm/StreamAccumulator.h
    src/llm/StreamAccumulator.cpp
    src/llm/HttpClient.h
    src/llm/HttpClient.cpp
    src/llm/LLMClient.h
    src/llm/LLMClient.cpp

    src/prompt/PromptContextBuilder.h
    src/prompt/PromptContextBuilder.cpp

    src/retrieval/VectorStore.h
    src/retrieval/VectorStore.cpp
    src/retrieval/IVectorStore.h
    src/retrieval/EmbeddingGenerator.h
    src/retrieval/EmbeddingGenerator.cpp
    src/retrieval/IEmbeddingGenerator.h
    src/retrieval/NovelChunker.h
    src/retrieval/NovelChunker.cpp

    src/server/SSEQueue.h
    src/server/SessionManager.h
    src/server/SessionManager.cpp
    src/server/BackendServer.h
    src/server/BackendServer.cpp

    src/cli/AnsiTerminal.h
    src/cli/IOutputChannel.h
    src/cli/ConsoleOutput.h
    src/cli/TerminalGUI.h
    src/cli/TerminalGUI.cpp

    src/agent/ToolRegistry.h
    src/agent/ToolRegistry.cpp
    src/agent/IToolProvider.h
    src/agent/IToolProvider.cpp
    src/agent/AgentOrchestratorTypes.h
    src/agent/ContextManagerTypes.h
    src/agent/ConversationSummarizer.h
    src/agent/ConversationSummarizer.cpp
    src/agent/ChapterSummaryCache.h
    src/agent/ChapterSummaryCache.cpp
    src/agent/DegradationPipeline.h
    src/agent/DegradationPipeline.cpp
    src/agent/SessionPersistence.h
    src/agent/SessionPersistence.cpp
    src/agent/ToolCallLoop.h
    src/agent/ToolCallLoop.cpp
    src/agent/IMessageProcessor.h
    src/agent/IMessageProcessor.cpp
    src/agent/ISynthesisStrategy.h
    src/agent/ISynthesisStrategy.cpp
    src/agent/AgentState.h
    src/agent/AgentState.cpp
    src/agent/ParameterValidator.h
    src/agent/ParameterValidator.cpp
    src/agent/ExecutionTracer.h
    src/agent/ExecutionTracer.cpp
    src/agent/ContextManager.h
    src/agent/ContextManager.cpp
    src/agent/Agent.h
    src/agent/Agent.cpp
    src/agent/ToolPipeline.h
    src/agent/ToolPipeline.cpp
    src/agent/PromptComposer.h
    src/agent/SubAgent.h
    src/agent/SubAgent.cpp
    src/agent/AgentOrchestrator.h
    src/agent/AgentOrchestrator.cpp
    src/agent/SubAgentTemplate.h
    src/agent/TemplateManager.h
    src/agent/TemplateManager.cpp
    src/agent/tools/BuiltInTool.h
    src/agent/tools/BuiltInTool.cpp
)

set(TOOLS_SOURCES
    src/agent/tools/ChapterTools.h
    src/agent/tools/ChapterTools.cpp
    src/agent/tools/CharacterTools.h
    src/agent/tools/CharacterTools.cpp
    src/agent/tools/SettingTools.h
    src/agent/tools/SettingTools.cpp
    src/agent/tools/WorldRuleTools.h
    src/agent/tools/WorldRuleTools.cpp
    src/agent/tools/OutlineTools.h
    src/agent/tools/OutlineTools.cpp
    src/agent/tools/ShellTools.h
    src/agent/tools/ShellTools.cpp
    src/agent/AgentSetup.h
    src/agent/AgentSetup.cpp
)

# 公共链接依赖
set(COMMON_LIBS
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Core 对象库
add_library(novelagent_core OBJECT ${CORE_SOURCES})
target_include_directories(novelagent_core PUBLIC src)
target_compile_definitions(novelagent_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(novelagent_core PUBLIC ${COMMON_LIBS})
target_precompile_headers(novelagent_core PRIVATE
    <string> <vector> <map> <algorithm> <sstream>
    <functional> <optional>
    <nlohmann/json.hpp> <spdlog/spdlog.h>
)

# Tools 对象库
add_library(novelagent_tools OBJECT ${TOOLS_SOURCES})
target_include_directories(novelagent_tools PUBLIC src)
target_link_libraries(novelagent_tools PUBLIC novelagent_core)
target_precompile_headers(novelagent_tools REUSE_FROM novelagent_core)

# CLI + App 聚合层（修改频率低）
set(APP_SOURCES
    src/cli/ReplHandler.h src/cli/ReplHandler.cpp
    src/cli/CommandParser.h src/cli/CommandParser.cpp
    src/cli/StreamDisplay.h src/cli/StreamDisplay.cpp
    src/NovelAgentApp.h src/NovelAgentApp.cpp
)

add_library(novelagent_app OBJECT ${APP_SOURCES})
target_include_directories(novelagent_app PUBLIC src)
target_link_libraries(novelagent_app PUBLIC novelagent_core novelagent_tools)
target_precompile_headers(novelagent_app REUSE_FROM novelagent_core)

# ============================================================================
# 主程序
# ============================================================================
add_executable(novelagent src/main.cpp)
target_link_libraries(novelagent PRIVATE novelagent_core novelagent_tools novelagent_app)

# ============================================================================
# 测试
# ============================================================================
enable_testing()
add_subdirectory(tests)
