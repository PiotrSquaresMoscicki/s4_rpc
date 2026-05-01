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

## Execution managers

`RpcEnvironment` delegates *when* commands run to an injectable
`IRpcManager<Context>`. This lets the same script be driven either
synchronously (one big batch) or one command per frame, which is useful
when testing controllers in MVC-style editors where some events only
reproduce when commands are split across ticks.

Two built-in managers ship in the header:

- **`ImmediateRpcManager<Context>`** *(default)* — `execute()` runs the
  whole script synchronously, exactly as before. `env.tick()` is a no-op.
- **`OneCommandPerTickRpcManager<Context>`** — `execute()` only enqueues
  the script. Each call to `env.tick()` advances the front script by
  exactly one command. `tick()` returns `true` while work is pending and
  `false` once the queue is drained.

Inject a manager via `env.set_manager(...)`; passing `nullptr` restores
the default. Scripts must outlive their executions — the typical
`parse_and_execute` macro produces a `static constexpr` AST, which
satisfies this automatically.

```cpp
rpc_dsl::RpcEnvironment<MyEditorCtx> env;
env.set_manager(std::make_unique<
    rpc_dsl::OneCommandPerTickRpcManager<MyEditorCtx>>());

static constexpr auto ast = rpc_dsl::ParseRpc<R"(
    OpenAsset("Map")
    SelectActor("Player")
    Translate(10, 0, 0)
)">();
env.execute(ast); // queued, nothing has run yet

while (env.tick()) {
    // one RPC command was just executed; engine ticks here
}
```

This shape integrates directly with Unreal Engine
`FAutomationLatentCommand`-style tests: `Update()` simply returns
`!env.tick();` to step one command per engine tick.
