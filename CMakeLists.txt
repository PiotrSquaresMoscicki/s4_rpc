cmake_minimum_required(VERSION 3.14)
project(rpc_dsl VERSION 1.0.0 LANGUAGES CXX)

# Library target (header-only)
add_library(rpc_dsl INTERFACE)
target_include_directories(rpc_dsl INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(rpc_dsl INTERFACE cxx_std_20)

# Tests
option(RPC_DSL_BUILD_TESTS "Build the unit tests" ON)
if(RPC_DSL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
