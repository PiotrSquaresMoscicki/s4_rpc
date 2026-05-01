include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

add_executable(rpc_dsl_test test.cpp)
target_link_libraries(rpc_dsl_test PRIVATE rpc_dsl doctest::doctest)

add_test(NAME rpc_dsl_test COMMAND rpc_dsl_test)
