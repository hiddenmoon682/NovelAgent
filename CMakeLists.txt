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
    ${NOVELAGENT_SKILL}
)
set(TOOLS_SOURCES ${NOVELAGENT_TOOLS})

set(COMMON_LIBS
    nlohmann_json::nlohmann_json spdlog::spdlog
    httplib::httplib OpenSSL::SSL OpenSSL::Crypto
    yaml-cpp::yaml-cpp
)

add_library(novelagent_core OBJECT ${CORE_SOURCES})
target_include_directories(novelagent_core PUBLIC src)
target_compile_definitions(novelagent_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
if(WIN32)
    # 与 Qt6::Platform 强制的 UNICODE/_UNICODE 保持一致：
    # 否则 GUI 目标（链接 Qt6 后带 -DUNICODE）与 novelagent_core 的 PCH
    # 宏不一致，GCC 拒绝复用 PCH（-Winvalid-pch “UNICODE is defined”）。
    # 项目内 Win32 调用均显式使用 A 后缀函数（如 CreateProcessA），
    # 此定义不改变任何运行时行为，仅修复 PCH 命中。
    target_compile_definitions(novelagent_core PUBLIC UNICODE _UNICODE)
endif()
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

# ── 测试专用预链接 STATIC 库 ──
# 29 个测试原先各自把 core/tools 的全部对象文件重新链接一遍；
# 预先归档成 .a 后，链接器按需拉取归档成员（core 无静态初始化副作用，
# 可安全惰性链接），显著缩短测试链接耗时。
# 注意：主程序目标（app/qt/gui）不受影响，仍直接链接 OBJECT 库。
# 使用要求（include/defs/依赖）需手动复制自 novelagent_core ——
# 由 $<TARGET_OBJECTS> 构造的 STATIC 库不继承源 OBJECT 库的 INTERFACE 属性。
add_library(novelagent_core_prelinked STATIC $<TARGET_OBJECTS:novelagent_core>)
target_include_directories(novelagent_core_prelinked PUBLIC src)
target_compile_definitions(novelagent_core_prelinked PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
if(WIN32)
    target_compile_definitions(novelagent_core_prelinked PUBLIC UNICODE _UNICODE)
endif()
target_link_libraries(novelagent_core_prelinked PUBLIC ${COMMON_LIBS})

# tools 预链接库：仅含 tools 对象，core 依赖经 core_prelinked 传递。
# 重要：tools 对象靠 REGISTER_TOOL 的文件级静态初始化自注册，
# 普通归档链接会丢弃未被直接引用的成员 → 工具注册消失。
# 整体包含由 tests/CMakeLists.txt 的 TOOLS_PRELINKED 用显式的
# --whole-archive/--no-whole-archive 标志对实现（不用
# $<LINK_LIBRARY:WHOLE_ARCHIVE,...>，它在 MinGW-GNU 上展开为
# --push-state/--pop-state，本工具链的 lld MinGW 前端不识别）。
add_library(novelagent_tools_prelinked STATIC $<TARGET_OBJECTS:novelagent_tools>)
target_link_libraries(novelagent_tools_prelinked PUBLIC novelagent_core_prelinked)

set(APP_SOURCES ${NOVELAGENT_APP})  # 定义于 cmake/Sources.cmake（集中登记）

add_library(novelagent_app OBJECT ${APP_SOURCES})
target_include_directories(novelagent_app PUBLIC src)
target_link_libraries(novelagent_app PUBLIC novelagent_core novelagent_tools)
target_precompile_headers(novelagent_app REUSE_FROM novelagent_core)

# ── 构建选项 ──
option(BUILD_GUI "Build Qt QML GUI executable" ON)

# ── Qt6 QML GUI 前端 ──
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)

    set(NOVELAGENT_QT_SOURCES
        src/novelagent_qt/QmlBridge.h src/novelagent_qt/QmlBridge.cpp
        src/novelagent_qt/QmlApp.h src/novelagent_qt/QmlApp.cpp
    )

    set(NOVELAGENT_QML_FILES
        src/novelagent_qt/qml/MainWindow.qml
        src/novelagent_qt/qml/SidebarPanel.qml
        src/novelagent_qt/qml/AgentPanel.qml
        src/novelagent_qt/qml/ChatBubble.qml
        src/novelagent_qt/qml/ToolCallCard.qml
        src/novelagent_qt/qml/ReaderPanel.qml
        src/novelagent_qt/qml/StatusBar.qml
        src/novelagent_qt/qml/SettingsDialog.qml
        src/novelagent_qt/qml/SkillPopup.qml
        src/novelagent_qt/qml/WelcomeWizard.qml
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
    # 复用 core 的 PCH（UNICODE 宏已在 novelagent_core 上统一，可安全复用）
    target_precompile_headers(novelagent_qt REUSE_FROM novelagent_core)

    qt_add_resources(novelagent_qt "qml_resources"
        PREFIX "/qml"
        BASE src/novelagent_qt/qml
        FILES ${NOVELAGENT_QML_FILES}
    )

    add_executable(novelagent_gui src/main_gui.cpp)
    target_include_directories(novelagent_gui PRIVATE src)
    target_link_libraries(novelagent_gui PRIVATE
        novelagent_core novelagent_tools novelagent_app novelagent_qt
        Qt6::Core Qt6::Quick Qt6::QuickControls2
    )
    target_precompile_headers(novelagent_gui REUSE_FROM novelagent_core)
endif()

# ── 自动复制 MSYS2 DLL（双击 exe 无需配置 PATH）──
if(WIN32 AND EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set(MSYS2_BIN "D:/SoftWare/msys2/mingw64/bin")
    set(NEEDED_DLLS
        libssl-3-x64.dll libcrypto-3-x64.dll
        libspdlog-1.17.dll libwinpthread-1.dll
        libgcc_s_seh-1.dll libstdc++-6.dll
        libyaml-cpp.dll
    )
    if(BUILD_GUI)
        foreach(dll ${NEEDED_DLLS})
            add_custom_command(TARGET novelagent_gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MSYS2_BIN}/${dll}" "$<TARGET_FILE_DIR:novelagent_gui>"
                COMMENT "Copy ${dll} -> novelagent_gui/"
            )
        endforeach()
    endif()
endif()

enable_testing()
add_subdirectory(tests)
