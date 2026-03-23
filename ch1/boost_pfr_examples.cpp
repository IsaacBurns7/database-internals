#include <iostream>
#include <string>
#include <sstream>
#include <vector>

// Requires Boost >= 1.68 and a C++17 compiler
// Compile: g++ -std=c++17 -I/path/to/boost boost_pfr_example.cpp -o boost_pfr_example
#include <boost/pfr.hpp>

// ─────────────────────────────────────────────
//  Sample aggregate structs (no constructors,
//  no private members — PFR requirement)
// ─────────────────────────────────────────────

struct Vec3 {
    float x;
    float y;
    float z;
};

struct PlayerRecord {
    int         id;
    float       health;
    float       speed;
    bool        alive;
};

// ─────────────────────────────────────────────
//  1. boost::pfr::get<N>
//     Access a specific field by compile-time index
// ─────────────────────────────────────────────

void demo_get() {
    std::cout << "=== boost::pfr::get<N> ===\n";

    Vec3 v{1.0f, 2.5f, -3.0f};

    float fx = boost::pfr::get<0>(v);   // x
    float fy = boost::pfr::get<1>(v);   // y
    float fz = boost::pfr::get<2>(v);   // z

    std::cout << "v.x = " << fx << "\n";
    std::cout << "v.y = " << fy << "\n";
    std::cout << "v.z = " << fz << "\n";

    // Works on arrays of structs too
    std::vector<Vec3> positions = {{0,0,0}, {1,2,3}, {4,5,6}};
    std::cout << "positions[1].y = "
              << boost::pfr::get<1>(positions[1]) << "\n\n";
}

// ─────────────────────────────────────────────
//  2. boost::pfr::tuple_element_t<N, T>
//     Resolve the type of the Nth field at compile time
// ─────────────────────────────────────────────

void demo_tuple_element_t() {
    std::cout << "=== boost::pfr::tuple_element_t<N, T> ===\n";

    // Grab the type of field 1 (float health) in PlayerRecord
    using HealthType = boost::pfr::tuple_element_t<1, PlayerRecord>;

    std::cout << "Field 1 of PlayerRecord is float? "
              << std::boolalpha
              << std::is_same_v<HealthType, float>
              << "\n";

    // Grab field 0 (int id)
    using IdType = boost::pfr::tuple_element_t<0, PlayerRecord>;
    std::cout << "Field 0 of PlayerRecord is int?   "
              << std::is_same_v<IdType, int>
              << "\n\n";
}

// ─────────────────────────────────────────────
//  3. boost::pfr::for_each_field
//     Iterate over every field with a generic callable
// ─────────────────────────────────────────────

void demo_for_each_field() {
    std::cout << "=== boost::pfr::for_each_field ===\n";

    PlayerRecord player{42, 87.5f, 5.2f, true};

    // Simple print of every field
    std::cout << "PlayerRecord fields: ";
    boost::pfr::for_each_field(player, [](const auto& field) {
        std::cout << field << "  ";
    });
    std::cout << "\n";

    // More advanced: build a CSV string using an index-aware overload
    std::ostringstream csv;
    boost::pfr::for_each_field(player, [&csv](const auto& field, std::size_t idx) {
        if (idx > 0) csv << ",";
        csv << field;
    });
    std::cout << "CSV: " << csv.str() << "\n\n";
}

// ─────────────────────────────────────────────
//  4. boost::pfr::tuple_size<T>::value
//     Number of fields in a struct, at compile time
// ─────────────────────────────────────────────

void demo_tuple_size() {
    std::cout << "=== boost::pfr::tuple_size<T>::value ===\n";

    constexpr std::size_t vec3_fields   = boost::pfr::tuple_size<Vec3>::value;
    constexpr std::size_t player_fields = boost::pfr::tuple_size<PlayerRecord>::value;

    std::cout << "Vec3         has " << vec3_fields   << " fields\n";
    std::cout << "PlayerRecord has " << player_fields << " fields\n\n";
}

// ─────────────────────────────────────────────
//  5. Putting it all together:
//     A generic struct-printer using all four features
// ─────────────────────────────────────────────

template <typename T>
void print_struct(const T& s) {
    constexpr std::size_t N = boost::pfr::tuple_size<T>::value;
    std::cout << "{ ";
    boost::pfr::for_each_field(s, [](const auto& field, std::size_t idx) {
        if (idx > 0) std::cout << ", ";
        std::cout << field;
    });
    std::cout << " }  [" << N << " fields]\n";
}

void demo_generic_printer() {
    std::cout << "=== Generic struct printer ===\n";

    Vec3         v{3.0f, 1.4f, 1.5f};
    PlayerRecord p{7, 100.0f, 3.8f, true};

    std::cout << "Vec3:         "; print_struct(v);
    std::cout << "PlayerRecord: "; print_struct(p);
    std::cout << "\n";
}

// ─────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────

int main() {
    demo_get();
    demo_tuple_element_t();
    demo_for_each_field();
    demo_tuple_size();
    demo_generic_printer();
    return 0;
}
