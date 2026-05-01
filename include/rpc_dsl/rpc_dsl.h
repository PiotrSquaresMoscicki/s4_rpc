#pragma once

#include <string_view>
#include <array>
#include <variant>
#include <string>
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <deque>

namespace rpc_dsl {

// --- 1. Compile-Time Utilities ---

constexpr std::string_view trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) end--;
    return s.substr(start, end - start);
}

constexpr int parse_int(std::string_view s) {
    int res = 0; bool neg = false; size_t i = 0;
    if (s.size() > 0 && s[0] == '-') { neg = true; i++; }
    for (; i < s.size(); i++) res = res * 10 + (s[i] - '0');
    return neg ? -res : res;
}

constexpr float parse_float(std::string_view s) {
    float res = 0; float frac = 1; bool decimal = false; bool neg = false; size_t i = 0;
    if (s.size() > 0 && s[0] == '-') { neg = true; i++; }
    for (; i < s.size(); i++) {
        if (s[i] == '.') { decimal = true; continue; }
        if (decimal) { frac /= 10.0f; res += (s[i] - '0') * frac; }
        else { res = res * 10.0f + (s[i] - '0'); }
    }
    return neg ? -res : res;
}

// --- 2. Compile-Time AST Structures ---

struct ConstexprArg {
    enum class Type { Int, Float, String, Variable } type = Type::Int;
    std::string_view str_val;
    int i_val = 0;
    float f_val = 0.0f;
};

struct Command {
    std::string_view assigned_var;
    std::string_view func_name;
    std::array<ConstexprArg, 8> args{};
    size_t arg_count = 0;
};

template <size_t N>
struct ParsedScript {
    std::array<Command, N> commands;
};

// --- 3. Constexpr Parser ---

constexpr size_t count_lines(std::string_view script) {
    size_t count = 0; bool in_cmd = false;
    for (char c : script) {
        if (c != '\n' && c != ' ' && c != '\t' && c != '\r') in_cmd = true;
        if (c == '\n' && in_cmd) { count++; in_cmd = false; }
    }
    return count + (in_cmd ? 1 : 0);
}

constexpr Command parse_line(std::string_view line) {
    Command cmd{};
    line = trim(line);
    if (line.empty()) return cmd;

    // Parse Assignment ("var name =")
    if (line.substr(0, 4) == "var ") {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string_view::npos) throw std::logic_error("Missing '=' in var assignment");
        cmd.assigned_var = trim(line.substr(4, eq_pos - 4));
        line = trim(line.substr(eq_pos + 1));
    }

    // Parse Function Name
    size_t paren_start = line.find('(');
    if (paren_start == std::string_view::npos) throw std::logic_error("Missing '('");
    cmd.func_name = trim(line.substr(0, paren_start));

    // Parse Arguments
    size_t paren_end = line.rfind(')');
    if (paren_end == std::string_view::npos) throw std::logic_error("Missing ')'");
    
    std::string_view args_str = trim(line.substr(paren_start + 1, paren_end - paren_start - 1));
    
    while (!args_str.empty() && cmd.arg_count < 8) {
        size_t comma = args_str.find(',');
        
        // Handle commas inside strings safely
        if (args_str[0] == '"') {
            size_t closing_quote = args_str.find('"', 1);
            if (closing_quote != std::string_view::npos) {
                comma = args_str.find(',', closing_quote);
            }
        }

        std::string_view arg_str = trim(args_str.substr(0, comma));
        ConstexprArg& arg = cmd.args[cmd.arg_count++];

        if (arg_str[0] == '"') { // String Literal
            arg.type = ConstexprArg::Type::String;
            arg.str_val = arg_str.substr(1, arg_str.size() - 2);
        } else {
            bool is_digit = (arg_str[0] >= '0' && arg_str[0] <= '9') || (arg_str[0] == '-' && arg_str.size() > 1);
            if (is_digit) {
                if (arg_str.find('.') != std::string_view::npos) {
                    arg.type = ConstexprArg::Type::Float;
                    arg.f_val = parse_float(arg_str);
                } else {
                    arg.type = ConstexprArg::Type::Int;
                    arg.i_val = parse_int(arg_str);
                }
            } else {
                arg.type = ConstexprArg::Type::Variable;
                arg.str_val = arg_str;
            }
        }

        if (comma == std::string_view::npos) break;
        args_str = trim(args_str.substr(comma + 1));
    }

    return cmd;
}

template <size_t N>
constexpr ParsedScript<N> parse_script(std::string_view script) {
    ParsedScript<N> result{};
    size_t cmd_idx = 0;
    size_t start = 0;
    while (start < script.size()) {
        size_t end = script.find('\n', start);
        if (end == std::string_view::npos) end = script.size();
        
        std::string_view line = trim(script.substr(start, end - start));
        if (!line.empty()) {
            result.commands[cmd_idx++] = parse_line(line);
        }
        start = end + 1;
    }
    return result;
}

// C++20 String Literal Bridge
template <std::size_t N>
struct StringLiteral {
    char value[N];
    constexpr StringLiteral(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) value[i] = str[i];
    }
    constexpr std::string_view view() const { return std::string_view(value, N - 1); }
};

template <StringLiteral Script>
constexpr auto ParseRpc() {
    constexpr std::string_view view = Script.view();
    constexpr std::size_t count = count_lines(view);
    return parse_script<count>(view);
}

#define parse_and_execute(LITERAL) execute(rpc_dsl::ParseRpc<LITERAL>())

// --- 4. Runtime Execution Environment ---

using RpcValue = std::variant<int, float, std::string>;
using RpcFunc = std::function<RpcValue(const std::array<RpcValue, 8>&, size_t)>;

template <typename Context>
struct RpcEnvironment;

template <typename Derived>
struct RpcContext {
    virtual ~RpcContext() = default;
    virtual void register_rpc_functions(RpcEnvironment<Derived>& env) = 0;
};

// --- 5. Resumable Script Execution State ---
//
// RpcExecution captures the program counter for an in-flight script so it
// can be advanced one command at a time. It stores the command sequence
// as a (pointer, size) view; the underlying ParsedScript<N> must outlive
// the execution. ParsedScript is typically a `static constexpr` produced
// by the parse_and_execute macro, which satisfies this requirement.
template <typename Context>
struct RpcExecution {
    RpcEnvironment<Context>* env = nullptr;
    const Command* commands = nullptr;
    size_t size = 0;
    size_t pc = 0;

    RpcExecution() = default;
    RpcExecution(RpcEnvironment<Context>* e, const Command* cmds, size_t n)
        : env(e), commands(cmds), size(n), pc(0) {}

    bool finished() const { return pc >= size; }

    // Executes exactly one non-empty command, advancing the program counter
    // past any blank entries. No-op when already finished.
    void step();

    void run_to_completion() {
        while (!finished()) step();
    }
};

template <typename Context>
struct RpcEnvironment {
    static_assert(std::is_base_of_v<RpcContext<Context>, Context>, "Context must derive from RpcContext<Context>");

    Context context;
    std::unordered_map<std::string, RpcValue> variables;
    std::unordered_map<std::string, RpcFunc> registry;

    // Pending executions driven by tick(). execute() bypasses this queue
    // and runs synchronously; submit() appends to it for one-command-per-tick
    // driving (e.g. Unreal Engine FAutomationLatentCommand::Update()).
    std::deque<RpcExecution<Context>> pending;

    RpcEnvironment() { context.register_rpc_functions(*this); }

    void set_context(Context&& ctx) {
        context = std::move(ctx);
        registry.clear();
        context.register_rpc_functions(*this);
    }

    template <typename Ret, typename... Args>
    void bind(const std::string& name, Ret (Context::*func)(Args...)) {
        registry[name] = [this, name, func](const std::array<RpcValue, 8>& args, size_t count) -> RpcValue {
            if (count != sizeof...(Args)) throw std::runtime_error("Argument count mismatch for: " + name);
            return invoke_with_args(func, args, std::index_sequence_for<Args...>{});
        };
    }

    // Run a parsed script to completion immediately, in the calling thread.
    // This is the synchronous, all-at-once mode and preserves the original
    // execute() behavior. It does not interact with the tick() queue.
    template <size_t N>
    void execute(const ParsedScript<N>& script) {
        RpcExecution<Context>(this, script.commands.data(), script.commands.size())
            .run_to_completion();
    }

    // Queue a parsed script for tick-driven execution. Nothing runs until
    // tick() is called. The script's storage must outlive its execution;
    // ParsedScript<N> produced by the parse_and_execute macro is typically
    // a `static constexpr`, which satisfies this naturally.
    template <size_t N>
    void submit(const ParsedScript<N>& script) {
        RpcExecution<Context> exec(this, script.commands.data(), script.commands.size());
        if (!exec.finished()) pending.push_back(std::move(exec));
    }

    // Advance the queued executions by exactly one command. Returns true
    // while work remains pending after this tick, false once the queue is
    // drained — chosen so UE FAutomationLatentCommand::Update() can write
    // `return !env.tick();`. Exceptions thrown by an RPC propagate out;
    // the offending execution is dropped from the queue.
    bool tick() {
        if (pending.empty()) return false;
        auto& front = pending.front();
        try {
            if (!front.finished()) front.step();
        } catch (...) {
            pending.pop_front();
            throw;
        }
        if (front.finished()) pending.pop_front();
        return !pending.empty();
    }

    // True when no tick-driven work remains.
    bool idle() const { return pending.empty(); }

    // Executes a single parsed command against this environment. Used by
    // RpcExecution::step(); also safe to call directly.
    void execute_command(const Command& cmd) {
        if (cmd.func_name.empty()) return;

        auto it = registry.find(std::string(cmd.func_name));
        if (it == registry.end()) throw std::runtime_error("Unknown function: " + std::string(cmd.func_name));

        std::array<RpcValue, 8> resolved_args;
        for (size_t i = 0; i < cmd.arg_count; ++i) {
            const auto& arg = cmd.args[i];
            if (arg.type == ConstexprArg::Type::Int) resolved_args[i] = arg.i_val;
            else if (arg.type == ConstexprArg::Type::Float) resolved_args[i] = arg.f_val;
            else if (arg.type == ConstexprArg::Type::String) resolved_args[i] = std::string(arg.str_val);
            else if (arg.type == ConstexprArg::Type::Variable) {
                std::string var_name(arg.str_val);
                if (variables.find(var_name) == variables.end()) throw std::runtime_error("Unknown variable: " + var_name);
                resolved_args[i] = variables.at(var_name);
            }
        }

        RpcValue result = it->second(resolved_args, cmd.arg_count);
        if (!cmd.assigned_var.empty()) {
            variables[std::string(cmd.assigned_var)] = result;
        }
    }

private:
    template <typename T>
    static T extract_arg(const RpcValue& val) {
        using CleanT = std::decay_t<T>;
        if constexpr (std::is_same_v<CleanT, float>) {
            if (std::holds_alternative<int>(val)) return static_cast<float>(std::get<int>(val));
        }
        return std::get<CleanT>(val);
    }

    template <typename Ret, typename... Args, std::size_t... Is>
    RpcValue invoke_with_args(Ret (Context::*func)(Args...), const std::array<RpcValue, 8>& args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<Ret>) {
            (context.*func)(extract_arg<Args>(args[Is])...);
            return 0;
        } else {
            return (context.*func)(extract_arg<Args>(args[Is])...);
        }
    }
};

// Out-of-line: needs the complete RpcEnvironment type.
template <typename Context>
void RpcExecution<Context>::step() {
    // Skip blank entries that the parser may leave between real commands.
    while (pc < size && commands[pc].func_name.empty()) ++pc;
    if (pc >= size) return;
    env->execute_command(commands[pc++]);
}

} // namespace rpc_dsl
