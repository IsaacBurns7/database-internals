#include <iostream>
#include <string>
#include <vector>
#include <tuple>

#include <boost/pfr.hpp>

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
struct Employee {
    int id;
    string name;
    string department;
    float salary;
    int age;
};

// --- Generic RowStore ---
template<typename T>
struct RowStore{
    int rows;
	vector<T> data;
    RowStore(int n) : rows(n), data(n) {}
	
	template<int N>
    auto& get(int row) { return boost::pfr::get<N>(data[row]); }
    template<int N, typename V>
    void set(int row, V&& val) { boost::pfr::get<N>(data[row]) = std::forward<V>(val); }
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const RowStore<T>& rs) {
    for (int i = 0; i < rs.rows; i++) {
        boost::pfr::for_each_field(rs.data[i], [&](const auto& field, auto idx) {
            constexpr auto N = boost::pfr::tuple_size<T>::value;
            os << field << (idx + 1 < N ? ", " : "\n");
        });
    }
    return os;
}

int main() {
// --- Row ---
    RowStore<Row> table(3);
    table.set<0>(0, 1); table.set<0>(1, 2); table.set<0>(2, 3);
    table.set<1>(0, "alice"); table.set<1>(1, "bob"); table.set<1>(2, "carol");
    table.set<2>(0, 50.0f); table.set<2>(1, 60.5f); table.set<2>(2, 72.1f);

    std::cout << "--- Rows ---\n" << table << '\n';

    // --- Point ---
    RowStore<Point> points(3);
    points.set<0>(0, 1.0f); points.set<0>(1, 3.0f); points.set<0>(2, 5.0f);
    points.set<1>(0, 2.0f); points.set<1>(1, 4.0f); points.set<1>(2, 6.0f);

    std::cout << "--- Points ---\n" << points << '\n';

    // --- Employee ---
    RowStore<Employee> employees(3);
    employees.set<0>(0, 1); employees.set<0>(1, 2); employees.set<0>(2, 3);
    employees.set<1>(0, "dave"); employees.set<1>(1, "eve"); employees.set<1>(2, "frank");
    employees.set<2>(0, "engineering"); employees.set<2>(1, "design"); employees.set<2>(2, "marketing");
    employees.set<3>(0, 95000.0f); employees.set<3>(1, 88000.0f); employees.set<3>(2, 76000.0f);
    employees.set<4>(0, 34); employees.set<4>(1, 28); employees.set<4>(2, 41);

    std::cout << "--- Employees ---\n" << employees << '\n';
}


