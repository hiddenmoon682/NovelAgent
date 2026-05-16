# Compiler-specific flags.
# On MSVC: /utf-8 to avoid C4819 warnings with Chinese comments in source.
# On GCC/Clang: strict warnings. -Wpedantic catches some C++20 extension issues.

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Debug: no optimization, full debug symbols.
# Release: O3 for throughput; the LLM latency dominates so O2 vs O3 is negligible,
# but O3 catches more aliasing/vectorization opportunities in JSON handling.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
