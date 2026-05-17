cmake_minimum_required(VERSION 3.20)
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES CXX
)

# C++20: we use std::format, std::span, concepts, designated initializers
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake modules in cmake/ directory
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerSettings)
include(FetchDependencies)

# === Phase 1 source files ===
# 文件显式列出（不用 glob），新文件必须手动添加，CMake 自动重新生成
set(SOURCES
    src/main.cpp

    # config — API keys, provider settings, JSON persistence
    src/config/AppConfig.h
    src/config/AppConfig.cpp

    # utils — thin wrappers over std::filesystem, strings, nlohmann
    src/utils/FileUtils.h
    src/utils/FileUtils.cpp
    src/utils/StringUtils.h
    src/utils/JsonUtils.h

    # project — Phase 1 data models and I/O
    src/project/Models.h
    src/project/ProjectIO.h
    src/project/ProjectIO.cpp
    src/project/ProjectManager.h
    src/project/ProjectManager.cpp

    # cli — stubs for Phase 3 (ReplHandler is the REPL core)
    src/cli/ReplHandler.h
    src/cli/ReplHandler.cpp
)

add_executable(novelagent ${SOURCES})
target_include_directories(novelagent PRIVATE src)

# Phase 1 依赖：全部 header-only 库，无需系统安装
target_link_libraries(novelagent PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
)

enable_testing()
add_subdirectory(tests)
