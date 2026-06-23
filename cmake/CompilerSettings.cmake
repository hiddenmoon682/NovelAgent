# Compiler-specific flags.
# On MSVC: /utf-8 to avoid C4819 warnings with Chinese comments in source.
# On GCC/Clang: strict warnings. -Wpedantic catches some C++20 extension issues.

# ── ccache 编译缓存（加速增量编译）──
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
    message(STATUS "Using ccache: ${CCACHE}")
    # pch_defines: 忽略 PCH 中嵌入的 __DATE__/__TIME__ 变化，避免无意义 cache miss
    # time_macros: 忽略源文件中 __TIME__/__DATE__/__TIMESTAMP__ 宏，提升命中率
    set(ENV{CCACHE_SLOPPINESS} "pch_defines,time_macros")
    execute_process(COMMAND ${CCACHE} --set-config sloppiness=pch_defines,time_macros
        OUTPUT_QUIET ERROR_QUIET)
    message(STATUS "ccache sloppiness: pch_defines,time_macros")
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -pipe)
    # 使用 lld 链接器（比 GNU ld 快 3-5x）
    add_link_options(-fuse-ld=lld)
    # Release: lld 链接器优化
    #   --icf=safe: 相同代码折叠（安全模式，保留指针唯一性语义）
    #   -O1:       基础链接时优化（哈希表/重定位，代价极低）
    add_link_options($<$<CONFIG:Release>:-Wl,--icf=safe>)
    add_link_options($<$<CONFIG:Release>:-Wl,-O1>)
    # 动态链接 C++ 运行时——spdlog/ftxui 只有 DLL 版本，
    # 混用静态运行时会导致 ODR 违规（双份 std::string/exception 实现）。
    # DLL 由 POST_BUILD 自动复制到 exe 旁边，无需手动分发。
endif()

# Debug: no optimization, full debug symbols.
# Release: O3 for throughput; the LLM latency dominates so O2 vs O3 is negligible,
# but O3 catches more aliasing/vectorization opportunities in JSON handling.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g0 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
