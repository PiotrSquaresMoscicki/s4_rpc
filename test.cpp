#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "rpc_dsl.h"

// --- Mock Context for Testing ---

struct TestContext : public rpc_dsl::RpcContext<TestContext> {
    int calls = 0;
    float last_x = 0.0f;
    std::string last_name = "";

    void register_rpc_functions(rpc_dsl::RpcEnvironment<TestContext>& env) override {
        env.bind("Spawn", &TestContext::Spawn);
        env.bind("MoveTo", &TestContext::MoveTo);
        env.bind("ValidateString", &TestContext::ValidateString);
    }

    int Spawn(std::string name, int initial_health) {
        calls++;
        last_name = name;
        return initial_health * 2; // Return manipulated value to test variable assignment
    }

    void MoveTo(int target_id, float x, float y) {
        calls++;
        last_x = x;
    }

    void ValidateString(std::string str) {
        calls++;
        last_name = str;
    }
};

// --- Test Cases ---

TEST_CASE("Compile-time Parser validation") {
    // This validates the constexpr parser produces the correct AST natively
    constexpr auto ast = rpc_dsl::ParseRpc<R"(
        var hero = Spawn("Knight", 100)
        MoveTo(hero, 15.5, -5)
    )">();

    static_assert(ast.commands[0].func_name == "Spawn");
    static_assert(ast.commands[0].assigned_var == "hero");
    static_assert(ast.commands[0].args[0].type == rpc_dsl::ConstexprArg::Type::String);
    static_assert(ast.commands[0].args[1].type == rpc_dsl::ConstexprArg::Type::Int);
    
    static_assert(ast.commands[1].func_name == "MoveTo");
    static_assert(ast.commands[1].args[0].type == rpc_dsl::ConstexprArg::Type::Variable);
    static_assert(ast.commands[1].args[1].f_val == 15.5f);
}

TEST_CASE("Runtime Execution and Variable State") {
    rpc_dsl::RpcEnvironment<TestContext> env;
    
    env.parse_and_execute(R"(
        var entity_id = Spawn("Dragon", 50)
        MoveTo(entity_id, 42.0, 10.0)
    )");

    // Context internal state should be updated
    CHECK(env.context.calls == 2);
    CHECK(env.context.last_name == "Dragon");
    CHECK(env.context.last_x == 42.0f);

    // Environment variable map should store the returned calculation (50 * 2)
    REQUIRE(env.variables.count("entity_id") == 1);
    CHECK(std::get<int>(env.variables["entity_id"]) == 100);
}

TEST_CASE("Type Coercion (Int to Float)") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    // MoveTo expects floats, but we pass integer literals (42 instead of 42.0)
    env.parse_and_execute(R"(
        MoveTo(1, 42, -10)
    )");

    CHECK(env.context.last_x == 42.0f); 
}

TEST_CASE("String Literal spaces parsing") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    // Tests that spaces inside strings are preserved and not trimmed
    env.parse_and_execute(R"(
        ValidateString("Hello World with Spaces")
    )");

    CHECK(env.context.last_name == "Hello World with Spaces");
}

TEST_CASE("Context Move Semantics") {
    rpc_dsl::RpcEnvironment<TestContext> env;
    
    TestContext custom_ctx;
    custom_ctx.calls = 100; // Simulate pre-existing complex state
    
    env.set_context(std::move(custom_ctx));
    
    env.parse_and_execute(R"(
        Spawn("Orc", 10)
    )");

    // Ensure the function was executed on the moved context
    CHECK(env.context.calls == 101); 
    CHECK(env.context.last_name == "Orc");
}
