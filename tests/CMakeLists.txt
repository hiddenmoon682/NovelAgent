cmake_minimum_required(VERSION 3.20)

# ============================================================================
# 所有测试目标复用 novelagent_lib 对象库的 .o 文件，消除重复编译。
# 每个测试只需：
#   1. 自己的 test_xxx.cpp
#   2. target_link_libraries(... PRIVATE novelagent_lib) — 自动继承 include 路径和依赖
#   3. target_precompile_headers(... REUSE_FROM novelagent_lib) — 复用 PCH
# ============================================================================

add_executable(test_main
    test_main.cpp
)
target_link_libraries(test_main PRIVATE novelagent_lib)
add_test(NAME test_main COMMAND test_main)
target_precompile_headers(test_main REUSE_FROM novelagent_lib)

add_executable(test_models
    test_models.cpp
)
target_link_libraries(test_models PRIVATE novelagent_lib)
add_test(NAME test_models COMMAND test_models)
target_precompile_headers(test_models REUSE_FROM novelagent_lib)

add_executable(test_project_io
    test_project_io.cpp
)
target_link_libraries(test_project_io PRIVATE novelagent_lib)
add_test(NAME test_project_io COMMAND test_project_io)
target_precompile_headers(test_project_io REUSE_FROM novelagent_lib)

add_executable(test_prompt_context
    test_prompt_context.cpp
)
target_link_libraries(test_prompt_context PRIVATE novelagent_lib)
add_test(NAME test_prompt_context COMMAND test_prompt_context)
target_precompile_headers(test_prompt_context REUSE_FROM novelagent_lib)

add_executable(test_token_counter
    test_token_counter.cpp
)
target_link_libraries(test_token_counter PRIVATE novelagent_lib)
add_test(NAME test_token_counter COMMAND test_token_counter)
target_precompile_headers(test_token_counter REUSE_FROM novelagent_lib)

add_executable(test_sse_parser
    test_sse_parser.cpp
)
target_link_libraries(test_sse_parser PRIVATE novelagent_lib)
add_test(NAME test_sse_parser COMMAND test_sse_parser)
target_precompile_headers(test_sse_parser REUSE_FROM novelagent_lib)

add_executable(test_llm_client
    test_llm_client.cpp
)
target_link_libraries(test_llm_client PRIVATE novelagent_lib)
add_test(NAME test_llm_client COMMAND test_llm_client)
target_precompile_headers(test_llm_client REUSE_FROM novelagent_lib)

add_executable(test_tool_registry
    test_tool_registry.cpp
)
target_link_libraries(test_tool_registry PRIVATE novelagent_lib)
add_test(NAME test_tool_registry COMMAND test_tool_registry)
target_precompile_headers(test_tool_registry REUSE_FROM novelagent_lib)

add_executable(test_agent
    test_agent.cpp
)
target_link_libraries(test_agent PRIVATE novelagent_lib)
add_test(NAME test_agent COMMAND test_agent)
target_precompile_headers(test_agent REUSE_FROM novelagent_lib)

add_executable(test_context_manager
    test_context_manager.cpp
)
target_link_libraries(test_context_manager PRIVATE novelagent_lib)
add_test(NAME test_context_manager COMMAND test_context_manager)
target_precompile_headers(test_context_manager REUSE_FROM novelagent_lib)

add_executable(test_chapter_tools
    test_chapter_tools.cpp
)
target_link_libraries(test_chapter_tools PRIVATE novelagent_lib)
add_test(NAME test_chapter_tools COMMAND test_chapter_tools)
target_precompile_headers(test_chapter_tools REUSE_FROM novelagent_lib)

# ── 测试运行时依赖标注 ──
# MSYS2 包（spdlog、fmt、OpenSSL）默认安装动态库，
# 测试 exe 运行时需要 DLL 在 PATH 中。追加 MSYS2 的 bin 目录。
# 对象库 PUBLIC 链接导致所有测试都依赖 MSYS2 动态库（spdlog、OpenSSL 等）。
# 全局追加 MSYS2 bin 到 PATH，避免逐个列举测试名。
if(EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set_property(TEST test_main test_models test_project_io test_prompt_context
                      test_token_counter test_sse_parser test_llm_client
                      test_tool_registry
                      test_agent
                      test_context_manager
                      test_chapter_tools
        APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:D:/SoftWare/msys2/mingw64/bin")
endif()

# ── 冒烟测试 — 需要真实 API Key，不在 CTest 中自动运行 ──
# 手动执行：./build/tests/test_deepseek_smoke.exe（需 config.json 或环境变量）
add_executable(test_deepseek_smoke
    test_deepseek_smoke.cpp
)
target_link_libraries(test_deepseek_smoke PRIVATE novelagent_lib)
target_precompile_headers(test_deepseek_smoke REUSE_FROM novelagent_lib)
