#ifndef INDEX_TREE

#define INDEX_TREE

#include "storage/disk_manager.h"
#include "common/config.h"

#include <optional>
#include <tuple>
#include <vector>
#include <cstring>

template <typename... Args>
class RecordType{
	std::tuple<Args...> fields; 
	RecordType(Args&&... args): fields(std::forward<Args>(args)...) {}
};

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

template <typename... Args>
class BPlusTree{
	//uses sibling pointers + strict min-key 
		//will implement latch crabbing for concurrency
	//database overall is IoT (Index Organized Tables) 
		//therefore caller must know Primary Index to Insert? 
		//... 
		//maybe we could have a RecordType parameter for the BPlusTree class, and then also define the primary index as part of the recordtype 
			//then they just insert via a RecordType (which is also the schema of the aforementioned table) 
	bool insert(RecordType<Args...> record);
		//RecordType is a parameter of the class (DELETE COMMENT WHEN IMPLEMENTED) 
	bool remove(RecordType<Args...> record); //could also just be keytype 
	std::optional<RecordType<Args...>> get(RecordType<Args...> record); //could also be keytype 
		//fielddescriptor is the keytype? 
		//if we allow multi-field keys, then we have to make a dedicated keytype 
		//we could just have keytype = fielddescriptor<T> 
    // range scan — returns all values where key is in [start, end]
    std::vector<RecordType<Args...>> scan(KeyType start, KeyType end); //could also be recordtype 
private:
	// splitChild(page_id_t parent_node, KeyType child); //could also be index 
		//take child, split into two. remember to add/update key stuff to parent node (strict min-key) 
	// merge(page_id_t parent_node, KeyType left_child); //could also be index, could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
	// redistribute(page_id_t parent_node, KeyType child); //could also be index 
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
	DiskManager* disk_manager_;
	//root page id is 0
};

#endif
