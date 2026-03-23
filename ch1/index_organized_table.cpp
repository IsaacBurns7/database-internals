#include <string>
#include <vector>
#include <iostream>
#include <map>
#include <unordered_map>
#include <variant>
#include <optional>
#include <sstream>
#include <boost/pfr.hpp>

using std::vector;
using std::cout;
using std::string;

struct Row {
    int id;
    string name;
    float income;
};

struct Employee {
    int id;
    string name;
    string department;
    float salary;
    int age;
};

// AnyIndex: a secondary index can be keyed by int, string, or float
template<typename Index_t>
using AnyIndex = std::variant<
    std::map<int,    vector<Index_t>>,
    std::map<string, vector<Index_t>>,
    std::map<float,  vector<Index_t>>
>;

template <typename Index_t, typename RecordStruct>
struct IndexOrganizedTable {
    int rows;
    std::map<Index_t, RecordStruct> data;
    std::unordered_map<int, AnyIndex<Index_t>> secondary_indexes;

    IndexOrganizedTable() : rows(0) {}

    void insert(Index_t key, RecordStruct val) {
        data[key] = val;
        rows++;
        for (auto& [field_pos, idx] : secondary_indexes) {
            update_secondary_index(idx, val, field_pos, key);
        }
    }

    bool exists(Index_t key) const { return data.count(key) > 0; }
    void remove(Index_t key)       { rows -= data.erase(key); }

    template<int N>
    auto& get(Index_t index) { return boost::pfr::get<N>(data[index]); }

    template<int N>
    const auto& get(Index_t index) const { return boost::pfr::get<N>(data[index]); }

    template<int N, typename V>
    void set(Index_t index, V&& val) { boost::pfr::get<N>(data[index]) = std::forward<V>(val); }

    // --- build secondary index on field N, typed correctly ---
    template<int N>
    void build_secondary_index() {
        using FieldType = boost::pfr::tuple_element_t<N, RecordStruct>;
        std::map<FieldType, vector<Index_t>> idx;
        for (const auto& [key, val] : data)
            idx[boost::pfr::get<N>(val)].push_back(key);
        secondary_indexes[N] = idx;
    }

    // --- secondary scan: void version ---
    template<int N, typename Func>
    void secondary_scan(
        const boost::pfr::tuple_element_t<N, RecordStruct>& from,
        const boost::pfr::tuple_element_t<N, RecordStruct>& to,
        Func func) const
    {
        using FieldType = boost::pfr::tuple_element_t<N, RecordStruct>;
        auto sit = secondary_indexes.find(N);
        if (sit == secondary_indexes.end()) {
            cout << "no secondary index on field " << N << '\n';
            return;
        }
        std::visit([&](const auto& idx) {
            using IdxKeyType = typename std::decay_t<decltype(idx)>::key_type;
            if constexpr (std::is_same_v<IdxKeyType, FieldType>) {
                auto begin = idx.lower_bound(from);
                auto end   = idx.upper_bound(to);
                for (auto it = begin; it != end; it++)
                    for (Index_t key : it->second)
                        func(data.at(key));
            }
        }, sit->second);
    }

    // --- secondary scan: accumulating version ---
    template<int N, typename Result, typename Func>
    Result secondary_scan(
        const boost::pfr::tuple_element_t<N, RecordStruct>& from,
        const boost::pfr::tuple_element_t<N, RecordStruct>& to,
        Result init, Func func) const
    {
        using FieldType = boost::pfr::tuple_element_t<N, RecordStruct>;
        auto sit = secondary_indexes.find(N);
        if (sit == secondary_indexes.end()) {
            cout << "no secondary index on field " << N << '\n';
            return init;
        }
        std::visit([&](const auto& idx) {
            using IdxKeyType = typename std::decay_t<decltype(idx)>::key_type;
            if constexpr (std::is_same_v<IdxKeyType, FieldType>) {
                auto begin = idx.lower_bound(from);
                auto end   = idx.upper_bound(to);
                for (auto it = begin; it != end; it++)
                    for (Index_t key : it->second)
                        init = func(init, data.at(key));
            }
        }, sit->second);
        return init;
    }

    // --- primary scans ---
    template<typename Func>
    void scan(Index_t from, Index_t to, Func func) const {
        auto begin = data.lower_bound(from);
        auto end   = data.upper_bound(to);
        for (auto it = begin; it != end; it++)
            func(it->second);
    }

    template<typename Result, typename Func>
    Result scan(Index_t from, Index_t to, Result init, Func func) const {
        auto begin = data.lower_bound(from);
        auto end   = data.upper_bound(to);
        for (auto it = begin; it != end; it++)
            init = func(init, it->second);
        return init;
    }

private:
	void update_secondary_index(AnyIndex<Index_t>& idx, const RecordStruct& val, int field_pos, Index_t key) {
        boost::pfr::for_each_field(val, [&](const auto& field, auto i) {
            if ((int)i == field_pos) {
                std::visit([&](auto& typed_idx) {
                    using IdxKeyType = typename std::decay_t<decltype(typed_idx)>::key_type;
                    using FieldType  = std::decay_t<decltype(field)>;
                    if constexpr (std::is_same_v<IdxKeyType, FieldType>)
                        typed_idx[field].push_back(key);
                }, idx);
            }
        });
    }
};

template<typename Index_t, typename RecordStruct>
std::ostream& operator<<(std::ostream& os, const IndexOrganizedTable<Index_t, RecordStruct>& iot) {
    for (const auto& [key, value] : iot.data) {
        boost::pfr::for_each_field(value, [&](const auto& field, auto idx) {
            constexpr auto N = boost::pfr::tuple_size<RecordStruct>::value;
            os << field << (idx + 1 < N ? ", " : "\n");
        });
    }
    return os;
}

int main() {
    // --- Row with int primary key ---
    IndexOrganizedTable<int, Row> table;
    table.insert(1, {1, "alice", 50.0f});
    table.insert(2, {2, "bob",   60.5f});
    table.insert(3, {3, "carol", 72.1f});

    float total_income = table.scan(1, 3, 0.0f, [](float acc, const Row& e){
        return acc + e.income;
    });
    cout << "--- Range scan total income: " << total_income << '\n';
    cout << "--- Rows by int key ---\n" << table << '\n';

    table.set<2>(1, 99.9f);
    cout << "--- After updating alice's income ---\n" << table << '\n';
    cout << "--- bob's name: " << table.get<1>(2) << " ---\n\n";

    // build secondary index on income (field 2, float)
    table.build_secondary_index<2>();
    cout << "--- income scan 60 to 100 ---\n";
    table.secondary_scan<2>(60.0f, 100.0f, [](const Row& r) {
        cout << r.name << ": " << r.income << '\n';
    });
    cout << '\n';

    // --- Employee with secondary index on department (field 2, string) ---
    IndexOrganizedTable<int, Employee> employees;
    employees.insert(1, {1, "alice", "engineering", 95000.0f, 34});
    employees.insert(2, {2, "bob",   "design",      88000.0f, 28});
    employees.insert(3, {3, "carol", "engineering", 76000.0f, 41});
    employees.insert(4, {4, "dave",  "marketing",   72000.0f, 25});
    employees.insert(5, {5, "eve",   "design",      91000.0f, 30});

    employees.build_secondary_index<2>();  // department (string)
    employees.build_secondary_index<3>();  // salary (float)

    cout << "--- all employees ---\n" << employees << '\n';

    cout << "--- design to engineering ---\n";
    employees.secondary_scan<2>("design", "engineering", [](const Employee& e) {
        cout << e.name << ", " << e.department << ", " << e.salary << '\n';
    });

    float total = employees.secondary_scan<2>("engineering", "engineering", 0.0f,
        [](float acc, const Employee& e) { return acc + e.salary; }
    );
    cout << "\nengineering salary total: " << total << '\n';

    // secondary scan on salary (float index)
    cout << "\n--- salary scan 80000 to 95000 ---\n";
    employees.secondary_scan<3>(80000.0f, 95000.0f, [](const Employee& e) {
        cout << e.name << ": " << e.salary << '\n';
    });
}
