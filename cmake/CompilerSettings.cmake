# Compiler-specific flags.
# On MSVC: /utf-8 to avoid C4819 warnings with Chinese comments in source.
# On GCC/Clang: strict warnings. -Wpedantic catches some C++20 extension issues.

# ── ccache 编译缓存（加速增量编译）──
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # ccache 按可执行名把 c++.exe 保守归类为 compiler_type=other，进而误触发
        # Clang 专用的 PCH mtime 硬检查（不受 sloppiness 控制）：清理重建后
        # cmake_pch.hxx mtime 变化 → PCH 必 miss → 全部 TU 级联 miss（实测命中率
        # 仅 0.8%）。内联声明真实类型 gcc 后清理重建可 100% 命中。
        # 用 launcher 内联参数（ccache 4.8+）而非 --set-config，避免污染用户级
        # 全局配置影响其它项目。
        list(APPEND CMAKE_C_COMPILER_LAUNCHER "compiler_type=gcc")
        list(APPEND CMAKE_CXX_COMPILER_LAUNCHER "compiler_type=gcc")
    endif()
    message(STATUS "Using ccache: ${CCACHE}")
    # pch_defines: 忽略 PCH 中嵌入的 __DATE__/__TIME__ 变化，避免无意义 cache miss
    # time_macros: 忽略源文件中 __TIME__/__DATE__/__TIMESTAMP__ 宏，提升命中率
    # include_file_mtime/ctime: 忽略被包含文件（含 .gch）的 mtime/ctime，改用
    #   内容哈希判定命中——清理重建后 PCH 时间戳变化不再导致全量 cache miss
    set(ENV{CCACHE_SLOPPINESS} "pch_defines,time_macros,include_file_mtime,include_file_ctime")
    execute_process(COMMAND ${CCACHE} --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
        OUTPUT_QUIET ERROR_QUIET)
    # max_size 10G：默认 5G 在全量构建 + PCH + 多分支切换下容易触发
    # LRU 淘汰，压低命中率；提升上限让历史对象文件留得住
    execute_process(COMMAND ${CCACHE} --set-config max_size=10G
        OUTPUT_QUIET ERROR_QUIET)
    message(STATUS "ccache sloppiness: pch_defines,time_macros,include_file_mtime,include_file_ctime; max_size: 10G")
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -pipe)
    # 使用 lld 链接器（比 GNU ld 快 3-5x）
    add_link_options(-fuse-ld=lld)
    # 死代码消除：把每个函数/数据拆到独立节，链接时剔除未被引用的节，
    # 可显著减小 exe 体积并让链接器处理更少输入（略加速链接）。
    # 与 --icf=safe 兼容；工具自注册（REGISTER_TOOL）依赖全局注册表引用，
    # 属正常符号引用，不会被 gc-sections 误删。
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
    # Release: lld 链接器优化
    #   --icf=safe: 相同代码折叠（安全模式，保留指针唯一性语义）
    #   -O1:       基础链接时优化（哈希表/重定位，代价极低）
    add_link_options($<$<CONFIG:Release>:-Wl,--icf=safe>)
    add_link_options($<$<CONFIG:Release>:-Wl,-O1>)
    # 动态链接 C++ 运行时——spdlog 只有 DLL 版本，
    # 混用静态运行时会导致 ODR 违规（双份 std::string/exception 实现）。
    # DLL 由 POST_BUILD 自动复制到 exe 旁边，无需手动分发。
endif()

# Debug: no optimization, full debug symbols.
# Release: O3 for throughput; the LLM latency dominates so O2 vs O3 is negligible,
# but O3 catches more aliasing/vectorization opportunities in JSON handling.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g0 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
