# Compiler-specific flags.
# On MSVC: /utf-8 to avoid C4819 warnings with Chinese comments in source.
# On GCC/Clang: strict warnings. -Wpedantic catches some C++20 extension issues.

# ── ccache 编译缓存（加速增量编译）──
find_program(CCACHE ccache)
if(CCACHE)
    # 缓存目录放在项目内的 .ccache，不用机器级的 %LOCALAPPDATA%\ccache：
    # 一是把可能长到 max_size=10G 的缓存挪出系统盘（C: 余量远小于 D:），
    # 二是让缓存随项目走，整体备份或清理时是一个目录的事，也天然不硬编码本机路径。
    # 用 ${CMAKE_SOURCE_DIR} 而非固定绝对路径：跨 worktree 共享缓存已被实测否证
    # （ccache 按绝对路径哈希 CWD，且 .gch 内容与预处理输出里的绝对路径无法归一，
    # 全新 worktree 首建 0/43 命中），所以"各树共用一份"既做不到也无收益；
    # 反而共用会让 worktree 写进一批主树永远命中不了的条目，白占 max_size 并触发 LRU
    # 淘汰掉有用的条目。每棵树各自一份才是对的，worktree 删掉时缓存随之消失。
    set(NOVELAGENT_CCACHE_DIR "${CMAKE_SOURCE_DIR}/.ccache"
        CACHE PATH "ccache 缓存目录（默认项目内 .ccache，可用 -D 覆盖）")
    file(MAKE_DIRECTORY "${NOVELAGENT_CCACHE_DIR}")
    # 必须在下面两条 --set-config 之前设：管理命令不认内联 key=value
    # （`ccache cache_dir=X --show-stats` 会报 Could not find compiler），
    # 只能靠环境变量指定写到哪个目录的 ccache.conf。
    set(ENV{CCACHE_DIR} "${NOVELAGENT_CCACHE_DIR}")

    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
    # cache_dir 也要走内联：set(ENV{}) 只在配置期生效，而单独一次
    # `cmake --build`（未触发 re-configure）不会重跑本文件，届时环境变量已不在，
    # 编译就会悄悄退回机器级默认目录。内联进 launcher 会被写进 build.ninja，恒定生效。
    list(APPEND CMAKE_C_COMPILER_LAUNCHER "cache_dir=${NOVELAGENT_CCACHE_DIR}")
    list(APPEND CMAKE_CXX_COMPILER_LAUNCHER "cache_dir=${NOVELAGENT_CCACHE_DIR}")
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
    # base_dir 曾在此处内联，理由是"让 git worktree 共用缓存"——该理由已被实测否证，
    # 故移除：全新 worktree 首建为 0/43 命中，改成 hash_dir=false 后仍是 0/43。
    # 根因是跨树的哈希输入无法归一：① ccache 按**绝对路径**哈希 CWD（调试日志
    # "Hashing CWD D:\...\build" vs "...\wt3\build"，manifest key 直接不同）；
    # ② 即便关掉 hash_dir，PCH 的 .gch 内容与预处理输出里的 `# 1 "<绝对路径>"`
    # 行标记仍带各树的绝对路径，base_dir 只归一命令行参数、管不到这些。
    # 结论：PCH 构建下跨目录/跨 worktree 共享缓存不可得，别再尝试用 base_dir 实现。
    message(STATUS "Using ccache: ${CCACHE} (cache_dir=${NOVELAGENT_CCACHE_DIR})")
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
    # -march=x86-64-v3：启用 AVX2 / BMI1 / BMI2 / FMA 代码生成。
    # 本工具链的 GCC 是按 --with-arch=nocona 配的（x86-64 基线，只有 SSE2），
    # 此前仅 novelagent_simdutf 单独拿到 -mavx2，于是 nlohmann::json 的字符串处理、
    # RAG 分块里的 std::string 查找这些热路径全在基线上跑、吃不到自动向量化。
    # 代价：产物要求 CPU 支持 AVX2（本机 i9-12900HX 实测有 avx2/bmi1/bmi2/fma），
    # 在更老的机器上会以非法指令崩溃。要给老机器分发就删掉这一行重编。
    # 副作用：simdutf 的 SIMDUTF_CAN_ALWAYS_RUN_HASWELL 会因此变为 1，
    # 运行时分派退化成直接用 haswell 内核——在 AVX2 机器上等价且更快，
    # 但不再有"老 CPU 回退到基线实现"这层保护，与上一行的取舍是同一件事。
    add_compile_options(-march=x86-64-v3)
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

# Debug: 不优化 + 完整调试符号。
# 这里原先追加的是 `-g0 -O0`，而 `-g0` 会覆盖 CMake 默认 Debug 旗标里的 `-g`
# （同一命令行后出现的胜），于是 C++ 目标文件一个 .debug 段都不生成——实测
# objdump -h 数 .debug* 为 0，gdb 对 main 报 "No line number information available"，
# 即 `gdb -batch` 只能给函数名、给不出 file:line 与局部变量。项目把 gdb 批处理
# 当作 AI 调试的主要手段，所以这一项必须留 `-g`。（注：C 文件走
# CMAKE_C_FLAGS_DEBUG，不受此影响，因此 test_*.exe 里仍能看到来自 sqlite3.c 的
# .debug 段，容易误判成"调试信息是有的"。）
# Release: O3 for throughput; the LLM latency dominates so O2 vs O3 is negligible,
# but O3 catches more aliasing/vectorization opportunities in JSON handling.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
