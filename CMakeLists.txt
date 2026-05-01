cmake_minimum_required(VERSION 3.14)
project(rpc_dsl VERSION 1.0.0 LANGUAGES CXX)
# Library target
add_library(rpc_dsl INTERFACE)
target_include_directories(rpc_dsl INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rpc_dsl INTERFACE cxx_std_20)
# Tests
option(RPC_DSL_BUILD_TESTS "Build the unit tests" ON)
if(RPC_DSL_BUILD_TESTS)
include(FetchContent)
FetchContent_Declare(
doctest
GIT_REPOSITORY https://github.com/doctest/doctest
GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)
add_executable(rpc_dsl_test test.cpp)
target_link_libraries(rpc_dsl_test PRIVATE rpc_dsl doctest::doctest)
# Enable CTest
enable_testing()
add_test(NAME rpc_dsl_test COMMAND rpc_dsl_test)
endif()
