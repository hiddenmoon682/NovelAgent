cmake_minimum_required(VERSION 3.20)

# 每个测试可执行文件独立，仅链接所需依赖。
# 命名约定：test_<模块名>.cpp → 可执行文件 test_<模块名>

# ── Phase 0: smoke test ──
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

# ── Phase 1: Models 序列化测试 ──
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

# ── Phase 1: ProjectIO 测试 ──
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
