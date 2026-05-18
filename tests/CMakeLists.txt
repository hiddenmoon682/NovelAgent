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
