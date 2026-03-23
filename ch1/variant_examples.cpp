#include <iostream>
#include <variant>
#include <string>

// ─────────────────────────────────────────────
// Overloaded helper (C++17 pattern matching)
// ─────────────────────────────────────────────
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;  // deduction guide (pre-C++20)


// ─────────────────────────────────────────────
// 1. Basic Usage
// ─────────────────────────────────────────────
void basic_usage() {
    std::cout << "=== Basic Usage ===\n";

    std::variant<int, double, std::string> v;

    v = 42;
    std::cout << "Assigned int:    " << std::get<int>(v) << "\n";

    v = 3.14;
    std::cout << "Assigned double: " << std::get<double>(v) << "\n";

    v = std::string("hello");
    std::cout << "Assigned string: " << std::get<std::string>(v) << "\n\n";
}


// ─────────────────────────────────────────────
// 2. Accessing Values
// ─────────────────────────────────────────────
void accessing_values() {
    std::cout << "=== Accessing Values ===\n";

    std::variant<int, double, std::string> v = 42;

    // std::get<T> — safe when type matches
    std::cout << "std::get<int>: " << std::get<int>(v) << "\n";

    // std::get<T> — throws on wrong type
    try {
        std::get<double>(v);
    } catch (const std::bad_variant_access& e) {
        std::cout << "std::get<double> threw: " << e.what() << "\n";
    }

    // std::get_if<T> — returns pointer or nullptr
    if (auto* p = std::get_if<int>(&v)) {
        std::cout << "std::get_if<int>: " << *p << "\n";
    }

    if (auto* p = std::get_if<double>(&v)) {
        std::cout << "std::get_if<double>: " << *p << "\n";
    } else {
        std::cout << "std::get_if<double>: nullptr (not active type)\n";
    }

    // std::visit — handles all types via lambda
    std::cout << "std::visit: ";
    std::visit([](auto& val) { std::cout << val << "\n"; }, v);

    std::cout << "\n";
}


// ─────────────────────────────────────────────
// 3. Error Handling Without Exceptions
// ─────────────────────────────────────────────
std::variant<int, std::string> parse(const std::string& s) {
    try {
        return std::stoi(s);
    } catch (...) {
        return std::string("parse error: \"" + s + "\" is not a number");
    }
}

void error_handling() {
    std::cout << "=== Error Handling ===\n";

    for (const auto& input : {"42", "abc", "100"}) {
        auto result = parse(input);
        if (auto* n = std::get_if<int>(&result))
            std::cout << "  \"" << input << "\" -> number: " << *n << "\n";
        else
            std::cout << "  \"" << input << "\" -> " << std::get<std::string>(result) << "\n";
    }

    std::cout << "\n";
}


// ─────────────────────────────────────────────
// 4. Overloaded Visitor (Pattern Matching)
// ─────────────────────────────────────────────
void overloaded_visitor() {
    std::cout << "=== Overloaded Visitor ===\n";

    std::variant<int, float, std::string> v;

    for (auto& var : {
        std::variant<int, float, std::string>{7},
        std::variant<int, float, std::string>{2.71f},
        std::variant<int, float, std::string>{std::string("world")}
    }) {
        std::visit(overloaded{
            [](int i)                { std::cout << "  int:    " << i << "\n"; },
            [](float f)              { std::cout << "  float:  " << f << "\n"; },
            [](const std::string& s) { std::cout << "  string: " << s << "\n"; }
        }, var);
    }

    std::cout << "\n";
}


// ─────────────────────────────────────────────
// 5. State Machine
// ─────────────────────────────────────────────
struct Idle {};
struct Running { int speed; };
struct Stopped { std::string reason; };

using State = std::variant<Idle, Running, Stopped>;

void print_state(const State& s) {
    std::visit(overloaded{
        [](const Idle&)           { std::cout << "  State: Idle\n"; },
        [](const Running& r)      { std::cout << "  State: Running at " << r.speed << " mph\n"; },
        [](const Stopped& st)     { std::cout << "  State: Stopped (" << st.reason << ")\n"; }
    }, s);
}

void state_machine() {
    std::cout << "=== State Machine ===\n";

    State state = Idle{};
    print_state(state);

    state = Running{60};
    print_state(state);

    state = Running{90};
    print_state(state);

    state = Stopped{"destination reached"};
    print_state(state);

    std::cout << "\n";
}


// ─────────────────────────────────────────────
// 6. Key Properties: index() and monostate
// ─────────────────────────────────────────────
void key_properties() {
    std::cout << "=== Key Properties ===\n";

    std::variant<int, double, std::string> v = 3.14;
    std::cout << "Active index (0=int, 1=double, 2=string): " << v.index() << "\n";

    // std::monostate — useful as a "nothing" / default state
    std::variant<std::monostate, int, std::string> opt;
    std::cout << "monostate index (unset): " << opt.index() << "\n";

    opt = 99;
    std::cout << "monostate index (after setting int): " << opt.index() << "\n";

    std::cout << "\n";
}


// ─────────────────────────────────────────────
// main
// ─────────────────────────────────────────────
int main() {
    basic_usage();
    accessing_values();
    error_handling();
    overloaded_visitor();
    state_machine();
    key_properties();

    return 0;
}
