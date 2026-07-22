cmake_minimum_required(VERSION 3.24)

# ── 修复 Git Bash 与 MSYS2 MinGW 的 DLL 冲突 ──
# Git for Windows 自带旧版 MinGW（/mingw64 → D:\git\Git\mingw64），
# 其 libstdc++-6.dll / libgcc_s_seh-1.dll 与 MSYS2 新版 ABI 不兼容。
# 当 Git Bash 是当前 shell 时，/mingw64/bin 被加入 PATH，
# 导致 cc1plus.exe 加载错误的 DLL 而崩溃。
# 必须在 project() 之前修复 PATH，否则编译器检测阶段就会失败。
# 检测方法：Git Bash 的 /mingw64 不含 c++ 编译器；MSYS2 的含有。
if(WIN32 AND NOT EXISTS "/mingw64/bin/c++.exe")
    string(REGEX REPLACE "/mingw64/bin:?" "" fixed_path "$ENV{PATH}")
    set(ENV{PATH} "${fixed_path}")
    message(STATUS "Git Bash detected: removed /mingw64/bin from PATH")
endif()

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

# Qt 6.8.3（QML GUI 前端）
if(EXISTS "D:/QT/QT/6.8.3/mingw_64")
    list(PREPEND CMAKE_PREFIX_PATH "D:/QT/QT/6.8.3/mingw_64")
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
    ${NOVELAGENT_CLI} ${NOVELAGENT_SKILL}
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

# ── Qt6 QML GUI 前端 ──
find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

set(NOVELAGENT_QT_SOURCES
    src/novelagent_qt/QmlBridge.h src/novelagent_qt/QmlBridge.cpp
    src/novelagent_qt/QmlApp.h src/novelagent_qt/QmlApp.cpp
)

set(NOVELAGENT_QML_FILES
    src/novelagent_qt/qml/MainWindow.qml
    src/novelagent_qt/qml/AgentPanel.qml
    src/novelagent_qt/qml/ChatBubble.qml
    src/novelagent_qt/qml/SessionPanel.qml
    src/novelagent_qt/qml/ReaderPanel.qml
    src/novelagent_qt/qml/StatusBar.qml
    src/novelagent_qt/qml/Theme.qml
    src/novelagent_qt/qml/qmldir
)

add_library(novelagent_qt OBJECT ${NOVELAGENT_QT_SOURCES})
target_include_directories(novelagent_qt PUBLIC src)
target_link_libraries(novelagent_qt PUBLIC
    novelagent_core novelagent_tools novelagent_app
    Qt6::Core Qt6::Quick Qt6::QuickControls2
)
set_target_properties(novelagent_qt PROPERTIES AUTOMOC ON)

qt_add_resources(novelagent_qt "qml_resources"
    PREFIX "/qml"
    BASE src/novelagent_qt/qml
    FILES ${NOVELAGENT_QML_FILES}
)

add_executable(novelagent src/main.cpp)
target_link_libraries(novelagent PRIVATE
    novelagent_core novelagent_tools novelagent_app novelagent_qt
    Qt6::Core Qt6::Quick Qt6::QuickControls2
)

# ── 自动复制 MSYS2 DLL（双击 exe 无需配置 PATH）──
if(WIN32 AND EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set(MSYS2_BIN "D:/SoftWare/msys2/mingw64/bin")
    set(NEEDED_DLLS
        libssl-3-x64.dll libcrypto-3-x64.dll
        libspdlog-1.17.dll libwinpthread-1.dll
        libgcc_s_seh-1.dll libstdc++-6.dll
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
