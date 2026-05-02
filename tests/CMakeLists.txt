include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

add_executable(rpc_dsl_test test.cpp test_stress_1000.cpp)
target_link_libraries(rpc_dsl_test PRIVATE rpc_dsl doctest::doctest)

# Register each doctest TEST_CASE as a separate ctest test so that ctest
# reports and runs them individually instead of treating the whole executable
# as a single test.
list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
include(doctest)
doctest_discover_tests(rpc_dsl_test)
