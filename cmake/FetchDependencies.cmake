# All third-party dependencies are fetched via FetchContent.
# This avoids requiring a system package manager (vcpkg/conan).
# The only exception is libcurl which we will NOT use — Phase 2 will
# use cpp-httplib (header-only, wraps WinHTTP on Windows) instead.

include(FetchContent)

# --- nlohmann/json ---
# The de-facto C++ JSON library. Header-only. Single include: <nlohmann/json.hpp>.
# We disable tests and install to speed up the fetch.
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

# --- CLI11 ---
# Command-line argument parsing. Header-only. Handles --help auto-generation.
# Used in main.cpp for --project, --exec, --provider, --verbose.
FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
)
set(CLI11_BUILD_TESTS OFF CACHE INTERNAL "")
set(CLI11_BUILD_EXAMPLES OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(cli11)

# --- spdlog ---
# Fast C++ logging. Header-only. Supports fmtlib-based formatting.
# We use the bundled fmt (SPDLOG_FMT_EXTERNAL=OFF) to avoid another dependency.
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
)
set(SPDLOG_FMT_EXTERNAL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(spdlog)
