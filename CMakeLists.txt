cmake_minimum_required(VERSION 3.16)
project(exchange_sim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

include_directories(include)

add_library(exsim_core
    src/order_book.cpp
    src/flat_order_book.cpp
    src/matching_engine.cpp
)

add_executable(demo src/main.cpp)
target_link_libraries(demo PRIVATE exsim_core)

add_executable(bench bench/benchmark.cpp)
target_link_libraries(bench PRIVATE exsim_core)
target_compile_options(bench PRIVATE -O3)

# ---- Tests (GoogleTest via FetchContent) ----
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Keep gtest's own runtime settings consistent with our project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(unit_tests
    tests/test_order_book.cpp
    tests/test_flat_order_book.cpp
    tests/test_matching_engine.cpp
    tests/test_matching_engine_typed.cpp
)
target_link_libraries(unit_tests PRIVATE exsim_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)
