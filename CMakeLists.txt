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

# === Phase 0 source files ===
# Files are listed explicitly (not globbed) so that adding a new file
# is a deliberate step — CMake re-runs when this file changes.
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

    # project — stubs for Phase 1 (Models.h will grow significantly)
    src/project/Models.h
    src/project/ProjectManager.h
    src/project/ProjectManager.cpp

    # cli — stubs for Phase 3 (ReplHandler is the REPL core)
    src/cli/ReplHandler.h
    src/cli/ReplHandler.cpp
)

add_executable(novelagent ${SOURCES})
target_include_directories(novelagent PRIVATE src)

# Only Phase 0 deps: header-only libraries. No system package required.
target_link_libraries(novelagent PRIVATE
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
)

enable_testing()
add_subdirectory(tests)
