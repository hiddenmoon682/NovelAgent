cmake_minimum_required(VERSION 3.20)

# Fix #7: 使用辅助函数减少重复
function(add_novelagent_test name libs)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${libs})
    add_test(NAME ${name} COMMAND ${name})
    target_precompile_headers(${name} REUSE_FROM novelagent_core)
endfunction()

# 仅依赖 core 的测试（链接预归档的 STATIC 库，惰性拉取成员，链接更快）
foreach(name test_main test_models test_project_io test_prompt_context
             test_token_counter test_sse_parser test_llm_client
             test_context_manager test_retrieval test_file_utils test_app_config
             test_parameter_validator test_thread_pool test_memory
             test_index_manifest test_id_utils test_rules_provider)
    add_novelagent_test(${name} novelagent_core_prelinked)
endforeach()

# 依赖 core + tools 的测试：tools 部分必须整归档链接（whole-archive）——
# 工具靠 REGISTER_TOOL 静态初始化自注册，普通归档链接会丢弃
# 未被直接引用的成员，导致注册表为空（core 依赖经 PUBLIC 传递，仍惰性链接）。
# 注：不用 $<LINK_LIBRARY:WHOLE_ARCHIVE,...> —— CMake 在 MinGW-GNU 上展开为
# --push-state/--pop-state，本工具链的 lld（MinGW 前端）不识别该参数；
# 改用显式的 --whole-archive/--no-whole-archive 标志对（链接项顺序保持）。
set(TOOLS_PRELINKED
    "-Wl,--whole-archive" novelagent_tools_prelinked "-Wl,--no-whole-archive")
foreach(name test_tool_registry test_agent test_chapter_tools
             test_character_tools
             test_setting_tools test_world_rule_tools
             test_outline_tools test_style_tools test_core_loop
             test_skill_registry test_search_memory_tools
             test_progressive_tool_provider)
    add_novelagent_test(${name} "${TOOLS_PRELINKED}")
endforeach()

# DLL 路径：从 C++ 编译器所在目录推导运行时 DLL 目录（如 msys2 的 mingw64/bin），
# 避免硬编码本机安装路径，保证跨机器可移植
get_filename_component(NOVELAGENT_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(WIN32 AND EXISTS "${NOVELAGENT_TOOLCHAIN_BIN}")
    set(CORE_TESTS test_main test_models test_project_io test_prompt_context
                   test_token_counter test_sse_parser test_llm_client
                   test_context_manager test_retrieval test_file_utils test_app_config
                   test_parameter_validator test_thread_pool test_memory
                   test_index_manifest test_id_utils test_rules_provider)
    set(TOOLS_TESTS test_tool_registry test_agent test_chapter_tools
                    test_character_tools
                    test_setting_tools test_world_rule_tools
                    test_outline_tools test_style_tools test_core_loop
                    test_skill_registry test_search_memory_tools
                    test_progressive_tool_provider)
    set_property(TEST ${CORE_TESTS} ${TOOLS_TESTS}
        APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${NOVELAGENT_TOOLCHAIN_BIN}")
endif()

# 手动执行测试（需 API Key）—— 同样改链预链接库
add_executable(test_deepseek_smoke test_deepseek_smoke.cpp)
target_link_libraries(test_deepseek_smoke PRIVATE "${TOOLS_PRELINKED}")
target_precompile_headers(test_deepseek_smoke REUSE_FROM novelagent_core)

add_executable(test_e2e_chapter test_e2e_chapter.cpp)
target_link_libraries(test_e2e_chapter PRIVATE "${TOOLS_PRELINKED}")
target_precompile_headers(test_e2e_chapter REUSE_FROM novelagent_core)

