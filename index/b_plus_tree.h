#ifndef INDEX_TREE

#define INDEX_TREE

#include "storage/disk_manager.h"
#include "common/config.h"

#include <optional>
#include <tuple>
#include <vector>
#include <cstring>
#include <variant> 
#include <string>
#include <concepts> 

using FieldVariant = std::variant<int, double, std::string>; 

template <typename T>
concept IsFieldVariant = std::same_as<T, FieldVariant>; 

template <IsFieldVariant... Args>
class RecordType{
	std::tuple<Args...> fields; 
	RecordType(Args&&... args): fields(std::forward<Args>(args)...) {}
};

//example usage: 
    // RecordType<FieldVariant, FieldVariant> record(
    //     FieldVariant{10}, 
    //     FieldVariant{"Hello"}
    // );

template <typename... Args>
class Record{
	static void serialize(char& buf, const RecordType<Args...>& record);
	static void deserialize(const char& buf, RecordType<Args...>& record);
};


template <typename T>
struct FieldDescriptor {
	std::string name;
	bool primary_index = false;
	// size_t size = sizeof(T); 
		//each slot in a "slottedpage" is a record in of itself, so I dont think BTree needs to know the size of the record itself, since its just going to be writing a slot to a slotted_page
		//restrict records to a constant size 
			//if an element of a record is variable-length, you will know because you have RecordType (or its template)
				//instead of recording the record itself, record the overflow page_id, and the slot_id at that overflow page
					//is the overflow page just a slotted page thats not TECHNICALLY part of the BTree? 
	void set_primary_index(){
		is_primary_index = true; 
	}
	bool is_primary_index(){
		return primary_index;
	}
};

template <typename T>
class FieldDescriptors{}; //intentionally undefined? WHY? 

template <typename... Args>
class FieldDescriptors<RecordType<Args...>> {
    std::tuple<FieldDescriptor<Args>...> descriptors;

    FieldDescriptors(std::array<std::string, sizeof...(Args)> names)
        : descriptors(make_descriptors(names, std::index_sequence_for<Args...>{}))
    {}

private:
    template<std::size_t... I>    // <-- deduced from index_sequence argument
    static std::tuple<FieldDescriptor<Args>...>
    make_descriptors(const std::array<std::string, sizeof...(Args)>& names,
                     std::index_sequence<I...>)  // <-- deduction happens here
    {
        return { FieldDescriptor<Args>{names[I]}... };
    }
};
//example usage: FieldDescriptors<RecordType<int, double, std::string>> fd;

//example usage: BPlusTree<0, int, float, std::string> tree; 
template <std::size_t KeyIdx, typename... Args>
	requires (KeyIdx < sizeof...(Args))
class BPlusTree {   // <-- no pack, exactly one value
    //IMPORTANT INFORMATION: ALL NODES ARE SLOTTED PAGES
		//INTERNAL NODE INFORMATION 
			//HEADER MUST CONTAIN 
				//key_max_len for # of bytes variable length keys can contain (= sizeof(keytype) for fixed-size keys)) 
				//overflow_page_id at byte min(size given by slot, key_max_len)
				//overflow_page_offset at byte min(size given by slot, key_max_len) + sizeof(overflow_page_id)
			//KIND OF SLOTS
				//POINTER: data is page_id:page_id_t 
				//KEY: data is [keytype_data][overflow_page_id][overflow_page_offset] 
					//if key is fixed-size overflow_page_id and overflow_page_offset are not added, and data is just [keytype_data] of size sizeof(keytype) 
		//LEAF NODE INFORMATION 
			//HEADER MUST CONTAIN 
				//nothing beyond normal header 
			//ONLY HAS RECORD SLOTS 
				//RecordType template will give you a generic templated type, which is a std variant determined at runtime
					//this is how recordtype can even exist 
	using KeyType = std::tuple_element_t<KeyIdx, std::tuple<Args...>>;
	//uses sibling pointers + strict min-key 
		//will implement latch crabbing for concurrency
	//database overall is IoT (Index Organized Tables) 
	bool insert(RecordType<Args...> record);
	bool remove(RecordType<Args...> record); 
	std::optional<RecordType<Args...>> get(KeyType target); 
    std::vector<RecordType<Args...>> scan(KeyType start, KeyType end); 
		// range scan — returns all values where key is in [start, end]
private:
	void splitChild(page_id_t parent_node, KeyType child); 
		//take child, split into two. 
		//remember to add/update key stuff to parent node (strict min-key) 
	void merge(page_id_t parent_node, KeyType left_child); //could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
	void redistribute(page_id_t parent_node, KeyType child);  
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
	DiskManager* disk_manager_;
	// uint16_t primary_key_index;
	uint32_t root_page_id; 
};

#endif
