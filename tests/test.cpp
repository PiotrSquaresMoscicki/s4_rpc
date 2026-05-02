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

TEST_CASE("parse_and_submit queues a script for tick-driven execution") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    env.parse_and_submit(R"(
        Spawn("X", 1)
        Spawn("Y", 2)
    )");

    // Like submit(), parse_and_submit only queues — nothing runs yet.
    CHECK(env.context.calls == 0);
    CHECK(env.idle() == false);

    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "X");

    CHECK(env.tick() == false);
    CHECK(env.context.last_name == "Y");
    CHECK(env.context.calls == 2);
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

// --- Runtime parser tests ---

TEST_CASE("Runtime parser executes scripts only known at runtime") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    // The script comes from a runtime std::string — this exercises the
    // string_view overload that parses at runtime instead of compile time.
    std::string script;
    script += "var entity_id = Spawn(\"Goblin\", 25)\n";
    script += "MoveTo(entity_id, 7.5, -3.0)\n";

    env.execute(std::string_view{script});

    CHECK(env.context.calls == 2);
    CHECK(env.context.last_name == "Goblin");
    CHECK(env.context.last_x == 7.5f);
    REQUIRE(env.variables.count("entity_id") == 1);
    CHECK(std::get<int>(env.variables["entity_id"]) == 50);
}

TEST_CASE("Runtime parser produces the same AST as the constexpr parser") {
    constexpr auto compile_time_ast = rpc_dsl::ParseRpc<R"(
        var hero = Spawn("Knight", 100)
        MoveTo(hero, 15.5, -5)
    )">();

    std::string runtime_script = R"(
        var hero = Spawn("Knight", 100)
        MoveTo(hero, 15.5, -5)
    )";
    rpc_dsl::RuntimeParsedScript runtime_ast = rpc_dsl::parse_runtime(runtime_script);

    REQUIRE(runtime_ast.commands.size() == 2);

    CHECK(runtime_ast.commands[0].func_name == compile_time_ast.commands[0].func_name);
    CHECK(runtime_ast.commands[0].assigned_var == compile_time_ast.commands[0].assigned_var);
    CHECK(runtime_ast.commands[0].args[0].type == compile_time_ast.commands[0].args[0].type);
    CHECK(runtime_ast.commands[0].args[1].i_val == compile_time_ast.commands[0].args[1].i_val);

    CHECK(runtime_ast.commands[1].func_name == compile_time_ast.commands[1].func_name);
    CHECK(runtime_ast.commands[1].args[0].type == compile_time_ast.commands[1].args[0].type);
    CHECK(runtime_ast.commands[1].args[1].f_val == compile_time_ast.commands[1].args[1].f_val);
}

TEST_CASE("Runtime parser supports submit() + tick() driven execution") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    // Build the script piece-by-piece at runtime to make absolutely sure
    // it cannot be a compile-time string literal.
    std::string script;
    script += "Spawn(\"A\", 1)\n";
    script += "Spawn(\"B\", 2)\n";
    script += "Spawn(\"C\", 3)\n";

    env.submit(std::string_view{script});

    // submit() owns the parsed AST internally, so the local `script`
    // string can go out of scope before all ticks fire.
    script.clear();
    script.shrink_to_fit();

    CHECK(env.context.calls == 0);
    CHECK(env.idle() == false);

    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "A");

    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "B");

    CHECK(env.tick() == false);
    CHECK(env.context.last_name == "C");
    CHECK(env.context.calls == 3);
    CHECK(env.idle() == true);
}

TEST_CASE("Runtime parser surfaces syntax errors as runtime exceptions") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    // Deliberately malformed script: missing the closing parenthesis on
    // the call. The constexpr parser would throw at compile time on a
    // literal; the runtime path throws std::logic_error when invoked.
    std::string bad = "Spawn(\"oops\", 1";
    CHECK_THROWS_AS(env.execute(std::string_view{bad}), std::logic_error);
}

// --- Appending steps to an in-flight execution ---
//
// Demonstrates that scripts can be pushed onto the pending queue while
// another script is mid-drain. Because tick() drains the front execution
// to completion before moving on, and `std::deque::push_back` does not
// invalidate references to existing elements, additional submit() calls
// — even from inside an RPC handler invoked by tick() — append cleanly
// to the back of the queue and run after the in-flight script finishes.

TEST_CASE("submit() between ticks appends commands to the live queue") {
    rpc_dsl::RpcEnvironment<TestContext> env;

    env.parse_and_submit(R"(
        Spawn("first", 1)
        Spawn("second", 2)
    )");

    // Drain the first command. The queue is still mid-flight.
    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "first");
    CHECK(env.idle() == false);

    // Append more steps now, while execution is in progress. The new
    // script lands at the *back* of the pending queue, so the in-flight
    // script keeps draining first.
    env.parse_and_submit(R"(
        Spawn("appended_a", 10)
        Spawn("appended_b", 20)
    )");

    // Finish draining the original script.
    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "second");

    // Then the appended script runs in submission order.
    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "appended_a");

    CHECK(env.tick() == false);
    CHECK(env.context.last_name == "appended_b");
    CHECK(env.context.calls == 4);
    CHECK(env.idle() == true);

    // Append again after the queue has fully drained — this is the
    // streaming use case where new commands arrive at any time.
    env.parse_and_submit(R"(
        Spawn("late", 99)
    )");
    CHECK(env.idle() == false);
    CHECK(env.tick() == false);
    CHECK(env.context.last_name == "late");
    CHECK(env.context.calls == 5);
    CHECK(env.idle() == true);
}

// Context that can re-enter submit() from within an RPC handler. This
// covers the trickier case where new commands are appended *while*
// tick() is mid-step. tick() holds a reference to the front execution
// across step(); std::deque::push_back is documented not to invalidate
// references to existing elements, so the in-flight execution is safe.
struct AppenderContext : public rpc_dsl::RpcContext<AppenderContext> {
    rpc_dsl::RpcEnvironment<AppenderContext>* env_ptr = nullptr;
    int calls = 0;
    std::string last_name = "";
    bool appended = false;

    void register_rpc_functions(rpc_dsl::RpcEnvironment<AppenderContext>& env) override {
        env.bind("Note", &AppenderContext::Note);
        env.bind("AppendThenNote", &AppenderContext::AppendThenNote);
    }

    void Note(std::string name) {
        calls++;
        last_name = name;
    }

    // Pushes a follow-up script onto the pending queue from inside an
    // RPC handler. This simulates "I learned about more work I need to
    // do while running" — e.g. an asset load triggering a follow-up
    // editor command sequence.
    void AppendThenNote(std::string name) {
        if (!appended && env_ptr) {
            appended = true;
            env_ptr->parse_and_submit(R"(
                Note("from_handler_a")
                Note("from_handler_b")
            )");
        }
        calls++;
        last_name = name;
    }
};

TEST_CASE("submit() from inside an RPC handler appends to the queue") {
    rpc_dsl::RpcEnvironment<AppenderContext> env;
    env.context.env_ptr = &env;

    env.parse_and_submit(R"(
        AppendThenNote("trigger")
        Note("after_trigger")
    )");

    // First tick runs AppendThenNote, which submits a follow-up script
    // mid-step. The follow-up must land at the back of the queue, after
    // the still-in-flight script.
    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "trigger");
    CHECK(env.context.appended == true);
    CHECK(env.idle() == false);

    // The original script's remaining command runs next, before the
    // appended one — FIFO ordering must hold even when the appender is
    // an RPC handler reached via tick().
    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "after_trigger");

    // Now the appended script drains.
    CHECK(env.tick() == true);
    CHECK(env.context.last_name == "from_handler_a");

    CHECK(env.tick() == false);
    CHECK(env.context.last_name == "from_handler_b");
    CHECK(env.context.calls == 4);
    CHECK(env.idle() == true);
}

// --- Regression: compile-time parsing must remain compile-time ---
//
// These static_asserts prove that scripts passed as string literals are
// parsed entirely during compilation. If a refactor ever accidentally
// routed the literal-form macros through the runtime parser, the
// expressions below would no longer be constant expressions and the
// translation unit would fail to compile — preventing the regression
// from reaching CI green.

static_assert(rpc_dsl::ParseRpc<R"(
    var hero = Spawn("Knight", 100)
    MoveTo(hero, 15.5, -5)
)">().commands[0].func_name == "Spawn",
    "ParseRpc<> must be evaluated at compile time");

static_assert(rpc_dsl::ParseRpc<R"(
    var hero = Spawn("Knight", 100)
    MoveTo(hero, 15.5, -5)
)">().commands[1].args[1].f_val == 15.5f,
    "ParseRpc<> must produce a constexpr AST");

// `parse_and_submit` already materializes the parsed AST as a
// `static constexpr` inside an immediately-invoked lambda. That value
// is itself a constant expression, so we can re-derive it here in a
// static_assert context to lock in the compile-time guarantee for the
// macro path as well.
static_assert([] {
    constexpr auto ast = rpc_dsl::ParseRpc<R"(
        Spawn("X", 1)
        Spawn("Y", 2)
    )">();
    return ast.commands[0].func_name == "Spawn"
        && ast.commands[1].args[0].str_val == "Y";
}(), "parse_and_submit / parse_and_execute literal path must be compile-time");
