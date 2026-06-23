cmake_minimum_required(VERSION 3.24)
project(NovelAgent
    DESCRIPTION "AI-powered novel writing assistant"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if(EXISTS "D:/SoftWare/msys2/mingw64")
    list(PREPEND CMAKE_PREFIX_PATH "D:/SoftWare/msys2/mingw64")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerSettings)

# OpenSSL 必须在 FetchDependencies 之前查找，
# 因为 cpp-httplib（HTTPLIB_USE_OPENSSL=ON）的 CMake 配置阶段就需要它
set(OPENSSL_ROOT_DIR "D:/SoftWare/msys2/mingw64" CACHE PATH "OpenSSL root directory")
if(NOT EXISTS "${OPENSSL_ROOT_DIR}")
    unset(OPENSSL_ROOT_DIR CACHE)
endif()
find_package(OpenSSL REQUIRED)

include(FetchDependencies)
include(Sources)  # Fix #1: 模块化源文件列表

# Fix #1: 模块化源文件列表（直接在 Sources.cmake 中定义变量，无需函数调用）
set(CORE_SOURCES
    ${NOVELAGENT_CONFIG} ${NOVELAGENT_UTILS} ${NOVELAGENT_PROJECT}
    ${NOVELAGENT_LLM} ${NOVELAGENT_RETRIEVAL} ${NOVELAGENT_AGENT}
    ${NOVELAGENT_CLI} ${NOVELAGENT_SERVER}
)
set(TOOLS_SOURCES ${NOVELAGENT_TOOLS})

set(COMMON_LIBS
    nlohmann_json::nlohmann_json CLI11::CLI11 spdlog::spdlog
    httplib::httplib OpenSSL::SSL OpenSSL::Crypto
)

add_library(novelagent_core OBJECT ${CORE_SOURCES})
target_include_directories(novelagent_core PUBLIC src)
target_compile_definitions(novelagent_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(novelagent_core PUBLIC ${COMMON_LIBS})
target_precompile_headers(novelagent_core PRIVATE
    <string> <vector> <map> <algorithm> <sstream>
    <functional> <optional>
    <nlohmann/json.hpp> <spdlog/spdlog.h>
)

add_library(novelagent_tools OBJECT ${TOOLS_SOURCES})
target_include_directories(novelagent_tools PUBLIC src)
target_link_libraries(novelagent_tools PUBLIC novelagent_core)
target_precompile_headers(novelagent_tools REUSE_FROM novelagent_core)

set(APP_SOURCES src/NovelAgentApp.h src/NovelAgentApp.cpp)

add_library(novelagent_app OBJECT ${APP_SOURCES})
target_include_directories(novelagent_app PUBLIC src)
target_link_libraries(novelagent_app PUBLIC novelagent_core novelagent_tools)
target_precompile_headers(novelagent_app REUSE_FROM novelagent_core)

add_library(novelagent_tui OBJECT ${NOVELAGENT_TUI})
target_include_directories(novelagent_tui PUBLIC src)
target_link_libraries(novelagent_tui PUBLIC novelagent_core ftxui::screen ftxui::dom ftxui::component)
target_precompile_headers(novelagent_tui REUSE_FROM novelagent_core)

add_executable(novelagent src/main.cpp)
target_link_libraries(novelagent PRIVATE novelagent_core novelagent_tools novelagent_app novelagent_tui)

# ── 自动复制 MSYS2 DLL（双击 exe 无需配置 PATH）──
if(WIN32 AND EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set(MSYS2_BIN "D:/SoftWare/msys2/mingw64/bin")
    set(NEEDED_DLLS
        libssl-3-x64.dll libcrypto-3-x64.dll
        libspdlog-1.17.dll libwinpthread-1.dll
        libgcc_s_seh-1.dll libstdc++-6.dll
        libftxui-screen.dll libftxui-dom.dll libftxui-component.dll
    )
    foreach(dll ${NEEDED_DLLS})
        add_custom_command(TARGET novelagent POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MSYS2_BIN}/${dll}" "$<TARGET_FILE_DIR:novelagent>"
            COMMENT "Copy ${dll} -> build/"
        )
    endforeach()
endif()

enable_testing()
add_subdirectory(tests)
