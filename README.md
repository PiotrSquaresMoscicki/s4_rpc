# s4_rpc

RPC DSL — a lightweight, single-header C++20 library for compile-time parsing
and runtime execution of an imperative, text-based RPC scripting language.

## Features

- **Constexpr Parsing**: The script string is parsed entirely during
  compilation. Syntax errors immediately halt the build.
- **Zero Runtime AST Allocation**: The Abstract Syntax Tree (AST) is baked
  directly into the binary as a fixed-size `std::array`.
- **CRTP Context Binding**: Bind standard C++ member functions to the scripting
  environment using clean Curiously Recurring Template Pattern (CRTP)
  interfaces.
- **Automatic Type Coercion**: Arguments dynamically resolve between integer
  and float based on the C++ function signature.

## Integration

Include `rpc_dsl/rpc_dsl.h` in your project. Requires a C++20 compatible
compiler.

## Repository layout

```
.
├── CMakeLists.txt
├── include/
│   └── rpc_dsl/
│       └── rpc_dsl.h        # Single-header library
├── tests/
│   ├── CMakeLists.txt
│   └── test.cpp             # doctest-based unit tests
└── build_and_test.sh        # Convenience build/test script
```

## Building and testing

Using the convenience script:

```sh
./build_and_test.sh
```

Or manually with CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Example usage

```cpp
#include "rpc_dsl/rpc_dsl.h"
#include <iostream>
#include <string>

struct GameContext : public rpc_dsl::RpcContext<GameContext> {
    void register_rpc_functions(rpc_dsl::RpcEnvironment<GameContext>& env) override {
        env.bind("Spawn", &GameContext::Spawn);
    }

    int Spawn(std::string name, int health) {
        std::cout << "Spawned " << name << " with " << health << " HP.\n";
        return 1;
    }
};

int main() {
    rpc_dsl::RpcEnvironment<GameContext> env;
    env.parse_and_execute(R"(
        var id = Spawn("Hero", 100)
    )");
    return 0;
}
```

## Execution modes

Every `RpcEnvironment` exposes two ways to run a script. They are always
available on the same instance — no scheduler injection or configuration
required — so a single test can directly compare the two.

- **`env.execute(script)`** *(synchronous)* — runs the whole script to
  completion in the calling thread. This is the default mode and matches
  the original behavior of the library.
- **`env.submit(script)` + `env.tick()`** *(tick-driven)* — `submit`
  queues the script; each call to `tick()` advances the front script by
  exactly one command. `tick()` returns `true` while work remains pending
  and `false` once the queue is drained, so Unreal Engine
  `FAutomationLatentCommand` implementations can write
  `Update() { return !env.tick(); }`. Use `env.idle()` to query the
  queue without advancing it.

Scripts must outlive their queued execution. The `parse_and_execute`
macro produces a `static constexpr` AST, which satisfies this
automatically; if you build a `ParsedScript<N>` by hand, keep it alive
until the queue is idle.

```cpp
rpc_dsl::RpcEnvironment<MyEditorCtx> env;

static constexpr auto ast = rpc_dsl::ParseRpc<R"(
    OpenAsset("Map")
    SelectActor("Player")
    Translate(10, 0, 0)
)">();

// Tick-driven: one RPC per engine tick.
env.submit(ast);
while (env.tick()) {
    // engine tick happens here
}

// Or synchronous: same script, all in one call.
env.execute(ast);
```

## Unreal Engine simple automation tests

The `submit` / `tick` / `idle` shape is designed to drop straight into a
UE *simple automation test* (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`). The
test body cannot block the engine, so any work that needs to span ticks
is queued through `ADD_LATENT_AUTOMATION_COMMAND` and driven by
`FAutomationLatentCommand::Update()` — which returns `true` when the
latent command is finished.

Because `env.tick()` returns `true` *while work remains* and `false`
once the queue is drained, a latent command can forward to the
environment with a single line: `return !Env.tick();`.

```cpp
// MyEditorRpcTest.cpp
#include "Misc/AutomationTest.h"
#include "rpc_dsl/rpc_dsl.h"

// 1) Your editor context exposes whatever the script needs to call.
struct FMyEditorCtx : public rpc_dsl::RpcContext<FMyEditorCtx> {
    void register_rpc_functions(rpc_dsl::RpcEnvironment<FMyEditorCtx>& Env) override {
        Env.bind("OpenAsset",   &FMyEditorCtx::OpenAsset);
        Env.bind("SelectActor", &FMyEditorCtx::SelectActor);
        Env.bind("Translate",   &FMyEditorCtx::Translate);
    }
    void OpenAsset(std::string Name)               { /* ... */ }
    void SelectActor(std::string Name)             { /* ... */ }
    void Translate(int X, int Y, int Z)            { /* ... */ }
};

// 2) A latent command that advances the environment by one RPC per
//    engine tick. Update() returns true when the command is done, so
//    we just negate env.tick().
class FTickRpcEnvLatentCommand : public IAutomationLatentCommand {
public:
    explicit FTickRpcEnvLatentCommand(rpc_dsl::RpcEnvironment<FMyEditorCtx>& InEnv)
        : Env(InEnv) {}

    bool Update() override { return !Env.tick(); }

private:
    rpc_dsl::RpcEnvironment<FMyEditorCtx>& Env;
};

// 3) The actual simple automation test.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMyEditorRpcTest,
    "MyEditor.Rpc.OpenSelectTranslate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMyEditorRpcTest::RunTest(const FString& /*Parameters*/) {
    // The environment must outlive every latent command that references
    // it, so own it on the heap and capture by reference.
    static rpc_dsl::RpcEnvironment<FMyEditorCtx> Env;

    // ParseRpc<...>() yields a constexpr AST; making it `static
    // constexpr` keeps it alive for the whole test run.
    static constexpr auto Ast = rpc_dsl::ParseRpc<R"(
        OpenAsset("Map")
        SelectActor("Player")
        Translate(10, 0, 0)
    )">();

    Env.submit(Ast);
    ADD_LATENT_AUTOMATION_COMMAND(FTickRpcEnvLatentCommand(Env));

    // Assertions about the post-conditions can be enqueued after the
    // tick-driven script with another latent command.
    return true;
}
```

If you instead want the script to run in a single tick (e.g. for fast
unit-style coverage) call `Env.execute(Ast)` directly inside `RunTest`
and skip the latent command — the same environment supports both modes.
