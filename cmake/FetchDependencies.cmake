# 第三方依赖管理。
# 优先查找系统包管理器安装的版本（MSYS2 pacman / vcpkg / 系统包）；
# 若未安装则自动回退到 FetchContent 从 GitHub 浅克隆源码编译。
# cpp-httplib 无 MSYS2 包，始终通过 FetchContent 获取。

include(FetchContent)

# --- nlohmann/json ---
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(json)
endif()

# --- CLI11 ---
find_package(CLI11 QUIET)
if(NOT CLI11_FOUND)
    FetchContent_Declare(cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.4.2
        GIT_SHALLOW TRUE
    )
    set(CLI11_BUILD_TESTS OFF CACHE INTERNAL "")
    set(CLI11_BUILD_EXAMPLES OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(cli11)
endif()

# --- spdlog ---
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
        GIT_SHALLOW TRUE
    )
    set(SPDLOG_FMT_EXTERNAL OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(spdlog)
endif()

# --- cpp-httplib ---
# 轻量级 C++ HTTP(S) 库，header-only。在 Windows 上封装 WinHTTP，
# Linux/macOS 上封装系统原生 socket API，无需 OpenSSL 即可访问 HTTPS。
# 用于 LLMClient 向 DeepSeek/Kimi/Claude API 发送 HTTP POST 请求。
FetchContent_Declare(cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.5
    GIT_SHALLOW TRUE
)
set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")
set(HTTPLIB_USE_OPENSSL ON CACHE INTERNAL "")
FetchContent_MakeAvailable(cpp-httplib)
