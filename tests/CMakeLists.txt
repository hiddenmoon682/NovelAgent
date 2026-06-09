cmake_minimum_required(VERSION 3.20)

# 分层链接：纯 core 测试只链 novelagent_core，需工具测试链 core+tools。
# 修改工具代码时，只有 tools 依赖的测试重链接，core 测试不受影响。

# ── 仅依赖 core 的测试 ──
set(CORE_ONLY_TESTS test_main test_models test_project_io test_prompt_context
                    test_token_counter test_sse_parser test_llm_client
                    test_context_manager test_retrieval)

foreach(name ${CORE_ONLY_TESTS})
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE novelagent_core)
    add_test(NAME ${name} COMMAND ${name})
    target_precompile_headers(${name} REUSE_FROM novelagent_core)
endforeach()

# ── 依赖 core + tools 的测试 ──
set(TOOLS_TESTS test_tool_registry test_agent test_chapter_tools
                test_character_tools test_sub_agent)

foreach(name ${TOOLS_TESTS})
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE novelagent_core novelagent_tools)
    add_test(NAME ${name} COMMAND ${name})
    target_precompile_headers(${name} REUSE_FROM novelagent_core)
endforeach()

# ── 测试运行时依赖标注 ──
if(EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set_property(TEST ${CORE_ONLY_TESTS} ${TOOLS_TESTS}
        APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:D:/SoftWare/msys2/mingw64/bin")
endif()

# ── 手动执行测试（需 API Key）──
add_executable(test_deepseek_smoke test_deepseek_smoke.cpp)
target_link_libraries(test_deepseek_smoke PRIVATE novelagent_core novelagent_tools)
target_precompile_headers(test_deepseek_smoke REUSE_FROM novelagent_core)

add_executable(test_e2e_chapter test_e2e_chapter.cpp)
target_link_libraries(test_e2e_chapter PRIVATE novelagent_core novelagent_tools)
target_precompile_headers(test_e2e_chapter REUSE_FROM novelagent_core)
