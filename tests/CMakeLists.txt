cmake_minimum_required(VERSION 3.20)

# Fix #7: 使用辅助函数减少重复
function(add_novelagent_test name libs)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${libs})
    add_test(NAME ${name} COMMAND ${name})
    target_precompile_headers(${name} REUSE_FROM novelagent_core)
endfunction()

# 仅依赖 core 的测试
foreach(name test_main test_models test_project_io test_prompt_context
             test_token_counter test_sse_parser test_llm_client
             test_context_manager test_retrieval test_file_utils test_app_config)
    add_novelagent_test(${name} novelagent_core)
endforeach()

# 依赖 core + tools 的测试
foreach(name test_tool_registry test_agent test_chapter_tools
             test_character_tools test_sub_agent)
    add_novelagent_test(${name} "novelagent_core;novelagent_tools")
endforeach()

# DLL 路径
if(EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set(CORE_TESTS test_main test_models test_project_io test_prompt_context
                   test_token_counter test_sse_parser test_llm_client
                   test_context_manager test_retrieval test_file_utils test_app_config)
    set(TOOLS_TESTS test_tool_registry test_agent test_chapter_tools
                    test_character_tools test_sub_agent)
    set_property(TEST ${CORE_TESTS} ${TOOLS_TESTS}
        APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:D:/SoftWare/msys2/mingw64/bin")
endif()

# 手动执行测试（需 API Key）
add_executable(test_deepseek_smoke test_deepseek_smoke.cpp)
target_link_libraries(test_deepseek_smoke PRIVATE novelagent_core novelagent_tools)
target_precompile_headers(test_deepseek_smoke REUSE_FROM novelagent_core)

add_executable(test_e2e_chapter test_e2e_chapter.cpp)
target_link_libraries(test_e2e_chapter PRIVATE novelagent_core novelagent_tools)
target_precompile_headers(test_e2e_chapter REUSE_FROM novelagent_core)
