cmake_minimum_required(VERSION 3.20)

# Phase 0: minimal smoke test to verify nlohmann/json links.
# Phase 1-2: will add test_project_io, test_sse_parser, test_token_counter.
# Test runner binary uses the same deps as main but not the main source files.

add_executable(test_runner
    test_main.cpp
)

target_include_directories(test_runner PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(test_runner PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_test(NAME test_runner COMMAND test_runner)
