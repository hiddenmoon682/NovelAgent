# 第三方依赖管理。
#
# 默认 OFF，且 **OFF 就是常态，不是待替换的临时状态**。
#
# 别把"翻成 ON"当成收尾工作：ON 的收益（任何机器、任何干净克隆都编出同一份依赖）
# 只在**存在第二个构建环境**时才成立——CI，或别人机器上的克隆。本机单机开发享受不到，
# 反而要付出一项真实代价：configure 从此依赖网络。本机到 GitHub 的连接已实测间歇性失败
# （git clone 反复 "Connection was reset, errno 10054" / 443 超时，curl 直接返回 000），
# 一旦 .deps-cache 丢失又赶上网络不通，配置就会硬失败；OFF 模式则完全不碰网络。
#
# 真到了需要跨机复现那天，先比较两条路再选，不要默认走 FetchContent：
#   - vendor 进 third_party/ —— 本项目对 cpp-httplib/sqlite/sqlite-vec/sqlitecpp/simdutf
#     已是这个做法，离线且确定，代价是仓库变大与手工升级；
#   - FetchContent（就是本文件的 ON 分支）—— 仓库不变大，代价是依赖网络。
#
# 另注：ON 分支**至今一次都没执行过**，未经取证。最可能出问题的是静态 spdlog 的编译
# 定义（SPDLOG_COMPILED_LIB 等）与 pacman 的 DLL 版不同，而 PCH 里包了 <spdlog/spdlog.h>；
# 宏集一变 GCC 就以 -Winvalid-pch 拒绝复用，打断这里跨 5 个目标的 REUSE_FROM。
#
# 当下真正的漂移防护是"可见"而非"不可能"：pin 已逐个对齐到实测在用的版本，配置期打印
# 实际取值，偏离已验证值就 message(WARNING)，并由 scripts/verify.sh 记进每次验证日志。
# 这里曾经的缺陷是注释与事实相反——find_package 优先且 pin 写着 v3.11.3 / v1.14.1 /
# 0.8.0，实际用的是 pacman 的 3.12.0 / 1.17.0 / 0.8.0.r78.ga83cd31，且构建目录里没有
# _deps，那条 pin 从未参与过构建、形同装饰。改到一致即可，不必为了"让 pin 生效"而换取法。
#
# cpp-httplib 是 third_party 下的本地 vendor 副本，始终直接 add_library。

# FETCHCONTENT_BASE_DIR 必须早于 include(FetchContent)：该模块会把默认值
# ${CMAKE_BINARY_DIR}/_deps 设成普通变量，后写的同名 CACHE 会被它遮蔽而失效
# （实测缓存目录仍落在 build/_deps）。
# 只在调用方没指定时才给默认值；预设里每项 cacheVariable 都会以 -D 重新下发，
# 因此它同时修掉了"上一轮配置把旧值钉在 CMakeCache 里"的问题。
# 缓存放在构建目录之外，是因为删 build/ 不应触发重新下载——本机网络是间歇性的。
if(NOT DEFINED FETCHCONTENT_BASE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps-cache"
        CACHE PATH "FetchContent 下载缓存（独立于构建目录，删 build 不必重新联网）")
endif()

include(FetchContent)

option(FETCH_PINNED_DEPS "按本文件钉死的版本取源码编译（需能访问 GitHub）" OFF)

# --- cpp-httplib ---
# 轻量级 C++ HTTP(S) 库，header-only。本地 vendor 副本，无需网络下载，
# 因此不受 FETCH_PINNED_DEPS 影响；别名供 COMMON_LIBS 使用。
add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib)
target_compile_definitions(httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto ws2_32 crypt32)
add_library(httplib::httplib ALIAS httplib)

# --- nlohmann/json ---
# v3.12.0 = 本机 MSYS2 mingw-w64-x86_64-nlohmann-json 的版本。
if(NOT FETCH_PINNED_DEPS)
    find_package(nlohmann_json QUIET)
endif()
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
    )
    # BuildTests 必须关：否则会把 json 的整个测试套件纳入构建。
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(json)
endif()

# --- spdlog ---
# v1.17.0 = 本机 MSYS2 mingw-w64-x86_64-spdlog 的版本。
# 走 FetchContent 后默认静态：不再受"pacman 只提供 DLL"的约束，
# CMakeLists.txt 的 NEEDED_DLLS 里 libspdlog-*.dll 也随之消失。
if(NOT FETCH_PINNED_DEPS)
    find_package(spdlog QUIET)
endif()
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    set(SPDLOG_BUILD_SHARED OFF CACHE INTERNAL "")
    set(SPDLOG_FMT_EXTERNAL OFF CACHE INTERNAL "")
    set(SPDLOG_INSTALL OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(spdlog)
endif()

# --- yaml-cpp ---
# 上游 0.8.0 之后没有再发版，MSYS2 打的是 master 快照（pkgver 0.8.0.r78.ga83cd31
# = git describe 0.8.0-78-ga83cd31）。所以 pin 到那个提交而不是 0.8.0 标签：
# 退回 0.8.0 会静默丢掉这 78 个提交里的解析修复，而 SKILL.md 的 frontmatter
# 解析正走这条路径。
if(NOT FETCH_PINNED_DEPS)
    find_package(yaml-cpp QUIET)
endif()
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG a83cd31548b19d50f3f983b069dceb4f4d50756d
        GIT_SHALLOW TRUE
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_TOOLS OFF CACHE INTERNAL "")
    set(YAML_CPP_INSTALL OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# ── 版本核对门 ──
# 走系统包时版本由 pacman 的滚动发布决定，仓库管不住；这里至少让偏离可见。
# 警告而非静默接受，是因为"换了 spdlog 小版本导致行为变化"这类问题一旦发生在
# 运行期，回溯成本远高于配置期多看一眼。
# 期望值即本次实测在用的版本；yaml-cpp 的 config-version 只报 0.8.0，看不出
# 它其实是 r78 快照，该差异由上面的 GIT_TAG 注释负责说明。
if(nlohmann_json_VERSION AND NOT nlohmann_json_VERSION VERSION_EQUAL 3.12.0)
    message(WARNING "nlohmann_json 版本偏离已验证值：${nlohmann_json_VERSION}（在用 3.12.0）")
endif()
if(spdlog_VERSION AND NOT spdlog_VERSION VERSION_EQUAL 1.17.0)
    message(WARNING "spdlog 版本偏离已验证值：${spdlog_VERSION}（在用 1.17.0）")
endif()
if(yaml-cpp_VERSION AND NOT yaml-cpp_VERSION VERSION_EQUAL 0.8.0)
    message(WARNING "yaml-cpp 版本偏离已验证值：${yaml-cpp_VERSION}（在用 0.8.0.r78.ga83cd31）")
endif()
