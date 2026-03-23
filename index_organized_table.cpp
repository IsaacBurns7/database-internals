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
    string name;
    string department;
    float salary;
    int age;
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
	
	// secondary indexes: field index -> (field value -> vector of primary keys)
    // store as map<string, map<string, vector<Index_t>>> keyed by field name
    // but since we dont have field names, key by field position instead
	unordered_map<int, map<string, vector<Index_t>>> secondary_indexes;
 
	IndexOrganizedTable(): rows(0){}

	void insert(RecordStruct val){
	    Index_t key = boost::pfr::get<0>(val);
		data[key] = val;
		rows++;
		//update secondary indexes
		for(auto& [field_pos, idx]: secondary_indexes){
			string field_val = get_field_as_string(val, field_pos);
			idx[field_val].push_back(key);
		}
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
	
	template<int N>
	void build_secondary_index(){
		map<string, vector<Index_t>> idx;
		for(const auto& [key, val]:data){
			string field_val = get_field_as_string(val, N);
			idx[field_val].push_back(key);	
		}	
		secondary_indexes[N] = idx;; 
	}
	
	// range scan on secondary index for field N
    template<int N, typename Func>
    void secondary_scan(const string& from, const string& to, Func func) const {
        auto sit = secondary_indexes.find(N);
        if (sit == secondary_indexes.end()) {
            cout << "no secondary index on field " << N << ", build it first\n";
            return;
        }
        const auto& idx = sit->second;
        auto begin = idx.lower_bound(from);
        auto end   = idx.upper_bound(to);
        for (auto it = begin; it != end; it++)
            for (Index_t key : it->second)
                func(data.at(key));
    }

    // accumulating secondary scan
    template<int N, typename Result, typename Func>
    Result secondary_scan(const string& from, const string& to, Result init, Func func) const {
        auto sit = secondary_indexes.find(N);
        if (sit == secondary_indexes.end()) {
            cout << "no secondary index on field " << N << ", build it first\n";
            return init;
        }
        const auto& idx = sit->second;
        auto begin = idx.lower_bound(from);
        auto end   = idx.upper_bound(to);
        for (auto it = begin; it != end; it++)
            for (Index_t key : it->second)
                init = func(init, data.at(key));
        return init;
    }
private:
    // helper: get field N of a struct as a string for secondary index storage
    string get_field_as_string(const RecordStruct& val, int n) const {
        string result;
        boost::pfr::for_each_field(val, [&](const auto& field, auto idx) {
            if (idx == n) {
                std::ostringstream oss;
                oss << field;
                result = oss.str();
            }
        });
        return result;
    }
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
    IndexOrganizedTable<int, Row> table;
    table.insert({1, "alice", 50.0f});
    table.insert({2, "bob",   60.5f});
    table.insert({3, "carol", 72.1f});
	
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


	IndexOrganizedTable<int, Employee> employees;
    employees.insert({1, "alice", "engineering", 95000.0f, 34});
    employees.insert({2, "bob",   "design",       88000.0f, 28});
    employees.insert({3, "carol", "engineering",  76000.0f, 41});
    employees.insert({4, "dave",  "marketing",    72000.0f, 25});
    employees.insert({5, "eve",   "design",        91000.0f, 30});

    // build secondary index on field 2 (department)
    employees.build_secondary_index<2>();

    cout << "--- all employees ---\n" << employees << '\n';

    // scan department "design" to "engineering"
    cout << "--- design to engineering ---\n";
    employees.secondary_scan<2>("design", "engineering", [](const Employee& e) {
        cout << e.name << ", " << e.department << ", " << e.salary << '\n';
    });

    // accumulate salaries in engineering
    float total = employees.secondary_scan<2>("engineering", "engineering", 0.0f,
        [](float acc, const Employee& e) { return acc + e.salary; }
    );
    cout << "\nengineering salary total: " << total << '\n';
}
