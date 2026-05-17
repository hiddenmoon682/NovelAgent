cmake_minimum_required(VERSION 3.20)
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES CXX
)

# 使用 C++20，项目里会用到 std::format、std::span、concepts 等特性。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 从 cmake/ 目录加载自定义模块。
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerSettings)
include(FetchDependencies)

# Phase 1 的源码列表。
# 明确列出文件而不是使用 glob，新增文件时需要手动加入，
# 这样 CMake 的输入边界更清晰，也更不容易漏掉构建变更。
set(SOURCES
    src/main.cpp

    # 配置模块：API Key、提供商设置与 JSON 持久化。
    src/config/AppConfig.h
    src/config/AppConfig.cpp

    # 工具模块：文件系统、字符串和 JSON 辅助函数。
    src/utils/FileUtils.h
    src/utils/FileUtils.cpp
    src/utils/StringUtils.h
    src/utils/JsonUtils.h

    # 项目模块：Phase 1 数据模型与项目 I/O。
    src/project/Models.h
    src/project/ProjectIO.h
    src/project/ProjectIO.cpp
    src/project/ProjectManager.h
    src/project/ProjectManager.cpp

    # 命令行模块：为 Phase 3 预留的 REPL 核心占位实现。
    src/cli/ReplHandler.h
    src/cli/ReplHandler.cpp
)

add_executable(novelagent ${SOURCES})
target_include_directories(novelagent PRIVATE src)

# Phase 1 的依赖目前都是 header-only 或轻量库，无需额外系统安装步骤。
target_link_libraries(novelagent PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
)

enable_testing()
add_subdirectory(tests)
