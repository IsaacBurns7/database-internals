#include <iostream>
#include <string>
#include <vector>
#include <tuple>

using std::string;
using std::vector;

// --- Your row structs ---
struct Row {
    int id;
    string name;
    float income;
};

struct Point {
    float x;
    float y;
};

// --- Traits: map each Row type to its column tuple type ---
template<typename T> struct ColumnTraits;

template<> struct ColumnTraits<Row> {
    using columns = std::tuple<vector<int>, vector<string>, vector<float>>;
    static columns make(int n) { return {vector<int>(n), vector<string>(n), vector<float>(n)}; }
};

template<> struct ColumnTraits<Point> {
    using columns = std::tuple<vector<float>, vector<float>>;
    static columns make(int n) { return {vector<float>(n), vector<float>(n)}; }
};

// --- Generic ColumnStore ---
template<typename T>
struct ColumnStore {
    int rows;
    typename ColumnTraits<T>::columns columns;

    ColumnStore(int n) : rows(n), columns(ColumnTraits<T>::make(n)) {}

    // get a specific column by index
    template<int N>
    auto& col() { return std::get<N>(columns); }

    template<int N>
    const auto& col() const { return std::get<N>(columns); }
};

// --- Helper to print all columns row by row ---
template<typename Tuple, std::size_t... Is>
void print_row(std::ostream& os, const Tuple& t, int i, std::index_sequence<Is...>) {
    ((os << std::get<Is>(t)[i] << (Is + 1 < sizeof...(Is) ? ", " : "")), ...);
    os << '\n';
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const ColumnStore<T>& cs) {
    constexpr auto N = std::tuple_size<typename ColumnTraits<T>::columns>::value;
    for (int i = 0; i < cs.rows; i++)
        print_row(os, cs.columns, i, std::make_index_sequence<N>{});
    return os;
}

int main() {
    ColumnStore<Row> table(3);
    table.col<0>() = {1, 2, 3};
    table.col<1>() = {"alice", "bob", "carol"};
    table.col<2>() = {50.0f, 60.5f, 72.1f};
    std::cout << table;

    std::cout << '\n';

    ColumnStore<Point> points(2);
    points.col<0>() = {1.0f, 3.0f};
    points.col<1>() = {2.0f, 4.0f};
    std::cout << points;
}
