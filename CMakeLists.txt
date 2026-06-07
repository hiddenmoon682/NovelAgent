cmake_minimum_required(VERSION 3.24)
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 若 MSYS2 存在，优先使用其包（本地开发加速）；
# CI 上该路径不存在，自动回退到 FetchContent 源码编译。
# 必须在 include(FetchDependencies) 之前设置，否则 FIND_PACKAGE_ARGS 无法命中。
if(EXISTS "D:/SoftWare/msys2/mingw64")
    list(PREPEND CMAKE_PREFIX_PATH "D:/SoftWare/msys2/mingw64")
endif()

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
    src/llm/StreamAccumulator.h
    src/llm/StreamAccumulator.cpp
    src/llm/LLMClient.h
    src/llm/LLMClient.cpp

    src/prompt/PromptContextBuilder.h
    src/prompt/PromptContextBuilder.cpp

    src/cli/ReplHandler.h
    src/cli/ReplHandler.cpp
)

# httplib HTTPS 需要 OpenSSL
set(OPENSSL_ROOT_DIR "D:/SoftWare/msys2/mingw64" CACHE PATH "OpenSSL root directory")
# 若缓存的路径不存在（如跨机器共享 build 目录），清理让 CMake 自动搜索
if(NOT EXISTS "${OPENSSL_ROOT_DIR}")
    unset(OPENSSL_ROOT_DIR CACHE)
endif()
find_package(OpenSSL REQUIRED)

add_executable(novelagent ${SOURCES})
target_include_directories(novelagent PRIVATE src)
target_link_libraries(novelagent PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

# 全局定义：httplib HTTPS 支持（所有含 #include <httplib.h> 的目标需要）
add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

# 预编译头（PCH）—— 加速编译。
# nlohmann/json.hpp（~2万行模板）被 8+ 个源文件分别解析，spdlog 被 5+ 个文件解析，
# PCH 将这些稳定头文件预编译为编译器快照，后续翻译单元直接加载，跳过重复解析。
target_precompile_headers(novelagent PRIVATE
    <string>
    <vector>
    <map>
    <algorithm>
    <sstream>
    <functional>
    <optional>
    <nlohmann/json.hpp>
    <spdlog/spdlog.h>
)

enable_testing()
add_subdirectory(tests)
