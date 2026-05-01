#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "rpc_dsl/rpc_dsl.h"

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

TEST_CASE("execute() runs scripts immediately (synchronous mode)") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    env.parse_and_execute(R"(
        Spawn("A", 1)
        Spawn("B", 2)
        Spawn("C", 3)
    )");

    // execute() is the all-at-once mode and ignores the tick queue.
    CHECK(env.context.calls == 3);
    CHECK(env.context.last_name == "C");
    CHECK(env.idle() == true);
    CHECK(env.tick() == false);
}

TEST_CASE("submit() + tick() advances exactly one command per tick") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    static constexpr auto ast = rpc_dsl::ParseRpc<R"(
        Spawn("A", 1)
        Spawn("B", 2)
        Spawn("C", 3)
    )">();
    env.submit(ast);

    // submit() only queues — nothing has run yet.
    CHECK(env.context.calls == 0);
    CHECK(env.idle() == false);

    CHECK(env.tick() == true);
    CHECK(env.context.calls == 1);
    CHECK(env.context.last_name == "A");

    CHECK(env.tick() == true);
    CHECK(env.context.calls == 2);
    CHECK(env.context.last_name == "B");

    // tick() returns true while work *remains pending after this tick*; on
    // the final command the queue drains, so the return is false.
    CHECK(env.tick() == false);
    CHECK(env.context.calls == 3);
    CHECK(env.context.last_name == "C");
    CHECK(env.idle() == true);

    // Further ticks remain idle.
    CHECK(env.tick() == false);
    CHECK(env.context.calls == 3);
}

TEST_CASE("execute() and submit() coexist on the same environment") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    static constexpr auto queued = rpc_dsl::ParseRpc<R"(
        Spawn("queued", 1)
    )">();
    env.submit(queued);
    CHECK(env.context.calls == 0);
    CHECK(env.idle() == false);

    // execute() runs synchronously and does not disturb the queue.
    env.parse_and_execute(R"(
        Spawn("now", 9)
    )");
    CHECK(env.context.calls == 1);
    CHECK(env.context.last_name == "now");
    CHECK(env.idle() == false);

    CHECK(env.tick() == false);
    CHECK(env.context.calls == 2);
    CHECK(env.context.last_name == "queued");
    CHECK(env.idle() == true);
}

TEST_CASE("Multiple submitted scripts are drained in order") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    static constexpr auto ast1 = rpc_dsl::ParseRpc<R"(
        Spawn("first", 1)
    )">();
    static constexpr auto ast2 = rpc_dsl::ParseRpc<R"(
        Spawn("second", 2)
        Spawn("third", 3)
    )">();
    env.submit(ast1);
    env.submit(ast2);

    CHECK(env.context.calls == 0);

    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "first");

    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "second");

    CHECK(env.tick() == false);
    CHECK(env.context.last_name == "third");
    CHECK(env.context.calls == 3);
    CHECK(env.idle() == true);
}
