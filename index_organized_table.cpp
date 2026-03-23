#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <map>
#include <boost/pfr.hpp>

using std::vector;
using std::cout;
using std::string;
using std::unordered_map;
using std::map; 

struct Employee {
    int id;
    string department;
    float salary;
};

struct Row {
	int id;
	string name; 
	float income;
};

template <typename Index_t, typename RecordStruct> 
struct IndexOrganizedTable { 
	int rows; 
	map<Index_t, RecordStruct> data; 
	IndexOrganizedTable(): rows(0){}

	void insert(Index_t key, RecordStruct val){
		data[key] = val;
		rows++;
	}	
	void exists(Index_t key) const { return data.count(key) > 0; }
	void remove(Index_t key) { rows -= data.erase(key); }
	
	template <typename Func>
	/* applies func to all records between from and to, expected to have type that can apply to the record */	
	void scan(Index_t from, Index_t to, Func func) const { 
		auto begin = data.lower_bound(from);
		auto end = data.upper_bound(to);
		for(auto it = begin; it != end; it++){
			func(it->second);
		}	
	}
	
	template <typename Result, typename Func>
	Result scan(Index_t from, Index_t to, Result init, Func func) const { 
		auto begin = data.lower_bound(from);
		auto end = data.upper_bound(to);
		for(auto it = begin; it != end; it++){
			init = func(init, it->second);
		}	
		return init; 
	}

	template<int N>
    auto& get(const Index_t& index) { return boost::pfr::get<N>(data[index]); }
    template<int N, typename V>
    void set(const Index_t& index, V&& val) { boost::pfr::get<N>(data[index]) = std::forward<V>(val); }
};

template<typename Index_t, typename RecordStruct>
std::ostream& operator<<(std::ostream& os, const IndexOrganizedTable<Index_t, RecordStruct>& iot){
    for (const auto& [key, value]: iot.data){
        boost::pfr::for_each_field(value, [&](const auto& field, auto idx) {
            constexpr auto N = boost::pfr::tuple_size<RecordStruct>::value;
            os << field << (idx + 1 < N ? ", " : "\n");
        });
    }
    return os;
}

int main(){
	// --- int key ---
    IndexOrganizedTable<int, Row> table;
    table.insert(1, {1, "alice", 50.0f});
    table.insert(2, {2, "bob",   60.5f});
    table.insert(3, {3, "carol", 72.1f});
	
	float total_income = table.scan(1, 3, 0.0f, [](float acc, const Row& e){
		return acc + e.income; 	
	});	
	cout << "--- Range scan says total income is: " << total_income << "\n"; 
    
	cout << "--- Rows by int key ---\n" << table << '\n';
    // --- update a field ---
    table.set<2>(1, 99.9f);
    cout << "--- After updating alice's income ---\n" << table << '\n';

    // --- get a field ---
    cout << "--- bob's name: " << table.get<1>(2) << " ---\n\n";

    // --- string key ---
    IndexOrganizedTable<string, Employee> employees;
    employees.insert("eng1",  {1, "engineering", 95000.0f});
    employees.insert("des1",  {2, "design",       88000.0f});
    employees.insert("mkt1",  {3, "marketing",    76000.0f});
	
	float salary_costs;
	employees.scan("eng1", "mkt1", [&salary_costs](const Employee& e){
		salary_costs += e.salary; 	
	});	
	cout << "--- Range scan says total salary is: " << salary_costs << "\n"; 

    cout << "--- Employees by string key ---\n" << employees << '\n';

    // --- lookup by key ---
    cout << "--- eng1 department: " << employees.get<1>("eng1") << " ---\n";
}
