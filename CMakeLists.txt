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

# ============================================================================
# httplib HTTPS 需要 OpenSSL
# ============================================================================
set(OPENSSL_ROOT_DIR "D:/SoftWare/msys2/mingw64" CACHE PATH "OpenSSL root directory")
# 若缓存的路径不存在（如跨机器共享 build 目录），清理让 CMake 自动搜索
if(NOT EXISTS "${OPENSSL_ROOT_DIR}")
    unset(OPENSSL_ROOT_DIR CACHE)
endif()
find_package(OpenSSL REQUIRED)

# ============================================================================
# 对象库 — 所有业务源码编译一次，测试和主程序共享 .o 文件
# ============================================================================

# 不含 main.cpp（其 main() 与测试的 main() 冲突）
set(LIB_SOURCES
    src/config/AppConfig.h
    src/config/AppConfig.cpp

    src/utils/FileUtils.h
    src/utils/FileUtils.cpp
    src/utils/StringUtils.h
    src/utils/JsonUtils.h
    src/utils/SchemaUtils.h

    src/project/Models.h
    src/project/ProjectIO.h
    src/project/ProjectIO.cpp
    src/project/ProjectManager.h
    src/project/ProjectManager.cpp

    src/llm/Message.h
    src/llm/StreamingTypes.h
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

    src/agent/tools/BuiltInTool.h
    src/agent/ToolRegistry.h
    src/agent/ToolRegistry.cpp
)

add_library(novelagent_lib OBJECT ${LIB_SOURCES})

# 使用 PUBLIC：消费者（主程序、测试）自动继承 include 路径和链接库
target_include_directories(novelagent_lib PUBLIC src)
target_compile_definitions(novelagent_lib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(novelagent_lib PUBLIC
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

# 预编译头 — 稳定的大型头文件预编译为快照，各翻译单元直接加载
# novelagent_lib 的 PCH 被所有测试目标 REUSE_FROM 共享
target_precompile_headers(novelagent_lib PRIVATE
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

# ============================================================================
# 主程序
# ============================================================================
add_executable(novelagent src/main.cpp)
target_link_libraries(novelagent PRIVATE novelagent_lib)

# ============================================================================
# 测试
# ============================================================================
enable_testing()
add_subdirectory(tests)
