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
