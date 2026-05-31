cmake_minimum_required(VERSION 3.20)

add_executable(test_main
    test_main.cpp
)
target_include_directories(test_main PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_main PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
add_test(NAME test_main COMMAND test_main)

add_executable(test_models
    test_models.cpp
)
target_include_directories(test_models PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_models PRIVATE
    nlohmann_json::nlohmann_json
)
add_test(NAME test_models COMMAND test_models)

add_executable(test_project_io
    test_project_io.cpp
    "${CMAKE_SOURCE_DIR}/src/project/ProjectIO.cpp"
    "${CMAKE_SOURCE_DIR}/src/project/ProjectManager.cpp"
    "${CMAKE_SOURCE_DIR}/src/project/Models.h"
    "${CMAKE_SOURCE_DIR}/src/utils/FileUtils.cpp"
)
target_include_directories(test_project_io PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_project_io PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
add_test(NAME test_project_io COMMAND test_project_io)

add_executable(test_prompt_context
    test_prompt_context.cpp
    "${CMAKE_SOURCE_DIR}/src/prompt/PromptContextBuilder.cpp"
    "${CMAKE_SOURCE_DIR}/src/project/ProjectIO.cpp"
    "${CMAKE_SOURCE_DIR}/src/project/ProjectManager.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/FileUtils.cpp"
)
target_include_directories(test_prompt_context PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_prompt_context PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
add_test(NAME test_prompt_context COMMAND test_prompt_context)

add_executable(test_token_counter
    test_token_counter.cpp
    "${CMAKE_SOURCE_DIR}/src/llm/TokenCounter.cpp"
)
target_include_directories(test_token_counter PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_token_counter PRIVATE
    nlohmann_json::nlohmann_json
)
add_test(NAME test_token_counter COMMAND test_token_counter)

add_executable(test_sse_parser
    test_sse_parser.cpp
    "${CMAKE_SOURCE_DIR}/src/llm/SSEParser.cpp"
)
target_include_directories(test_sse_parser PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_sse_parser PRIVATE
    nlohmann_json::nlohmann_json
)
add_test(NAME test_sse_parser COMMAND test_sse_parser)

add_executable(test_llm_client
    test_llm_client.cpp
    "${CMAKE_SOURCE_DIR}/src/llm/LLMClient.cpp"
    "${CMAKE_SOURCE_DIR}/src/llm/SSEParser.cpp"
    "${CMAKE_SOURCE_DIR}/src/llm/StreamAccumulator.cpp"
)
target_include_directories(test_llm_client PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_llm_client PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)
add_test(NAME test_llm_client COMMAND test_llm_client)

# ── 测试运行时依赖标注 ──
# MSYS2 包（spdlog、fmt、OpenSSL）默认安装动态库，
# 测试 exe 运行时需要 DLL 在 PATH 中。追加 MSYS2 的 bin 目录。
if(EXISTS "D:/SoftWare/msys2/mingw64/bin")
    set_property(TEST test_main test_project_io test_prompt_context
                      test_llm_client
        APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:D:/SoftWare/msys2/mingw64/bin")
endif()

# 冒烟测试 — 需要真实 API Key，不在 CTest 中自动运行。
# 手动执行：./build/tests/test_deepseek_smoke.exe（需 config.json 或环境变量）
add_executable(test_deepseek_smoke
    test_deepseek_smoke.cpp
    "${CMAKE_SOURCE_DIR}/src/llm/LLMClient.cpp"
    "${CMAKE_SOURCE_DIR}/src/llm/SSEParser.cpp"
    "${CMAKE_SOURCE_DIR}/src/llm/StreamAccumulator.cpp"
    "${CMAKE_SOURCE_DIR}/src/config/AppConfig.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/FileUtils.cpp"
)
target_include_directories(test_deepseek_smoke PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(test_deepseek_smoke PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)
