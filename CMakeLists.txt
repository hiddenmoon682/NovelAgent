cmake_minimum_required(VERSION 3.20)
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerSettings)
include(FetchDependencies)

# 明确列出源文件，避免 glob 带来的构建边界不清晰问题。
set(SOURCES
    src/main.cpp

    src/config/AppConfig.h
    src/config/AppConfig.cpp

    src/utils/FileUtils.h
    src/utils/FileUtils.cpp
    src/utils/StringUtils.h
    src/utils/JsonUtils.h

    src/project/Models.h
    src/project/ProjectIO.h
    src/project/ProjectIO.cpp
    src/project/ProjectManager.h
    src/project/ProjectManager.cpp

    src/llm/Message.h
    src/llm/TokenCounter.h
    src/llm/TokenCounter.cpp
    src/llm/SSEParser.h
    src/llm/SSEParser.cpp

    src/prompt/PromptContextBuilder.h
    src/prompt/PromptContextBuilder.cpp

    src/cli/ReplHandler.h
    src/cli/ReplHandler.cpp
)

add_executable(novelagent ${SOURCES})
target_include_directories(novelagent PRIVATE src)
target_link_libraries(novelagent PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
    httplib::httplib
)

enable_testing()
add_subdirectory(tests)
