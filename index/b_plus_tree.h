#ifndef INDEX_TREE

#define INDEX_TREE

#include "storage/disk_manager.h"
#include "common/config.h"

#include "optional"
#include "tuple"

template <typename T>
struct FieldDescriptor {
	std::string name;
	bool is_primary_index = false;
	// size_t size = sizeof(T); 
		//each slot in a "slottedpage" is a record in of itself, so I dont think BTree needs to know the size of the record itself, since its just going to be writing a slot to a slotted_page
		//restrict records to a constant size 
			//if an element of a record is variable-length, you will know because you have RecordType (or its template)
				//instead of recording the record itself, record the overflow page_id, and the slot_id at that overflow page
					//is the overflow page just a slotted page thats not TECHNICALLY part of the BTree? 
	set_primary_index(){
		is_primary_index = true; 
	}
	bool is_primary_index(){
		return is_primary_index;
	}
};
//specialization for std::string


template <typename T>
class FieldDescriptors; //intentionally undefined? WHY? 

template <typename... Args>
class FieldDescriptors<RecordType<Args...>>{
	std::tuple<FieldDescriptor<Args>...> descriptors;
public:
	FieldDescriptors(std::string... names): descriptors(FieldDescriptor<Args>{names}...) {}
};
//example usage: FieldDescriptors<RecordType<int, double, std::string>> fd;

//proper like field_descriptors too complex, currently first field is just gonna be assumed as index, no secondary indexes
template <typename... Args>
class RecordType{
	std::tuple<Args...> fields; 
	RecordType(Args&&... args): fields(std::forward<Args>(args)...) {}
};


template <typename... Args>
class Record{
	//maybe raw recordtype and then friend functions? 
	//parameterized friend functions?
	//is there a way for BPlusTree to "own" the schema, and then it applies that schema every time it serializes or deserializes through the RecordType class 
		//is this asking for too much from BPlusTree though? 
	//is there a way for BPlusTree to create an instance of RecordType, and then it serializes and deserializes for BPlusTree
		//how is this useful? 
			//serialization is obviously useful, but much easier 
			//deserialization will mostly happen in get and scan
					//should deserialization even be necessarily seperate from the BPlusTree? 
	// std::vector<FieldDescriptor> field_descriptors;
		//figure out way to take in FDs 
		//maybe FDs are in B_Tree class? 
			//or in separate FDs class, and then trust B_Tree has one template it gives to both FD and Record class 
	static void serialize(char& buf, const RecordType<Args...>& record){
		std::apply([](auto&&... args){
			([](auto&& x){
				using T = std::decay_t<decltype(x)>;
				if constexpr (std::is_arithmetic_v<T>){
					//write to buf
					std::memcpy(buf, &x, sizeof(T));
					buf += sizeof(T);
				}else{
					//add uint32_t (4 bytes) for size, then the element 
					//probably have to figure out its size dynamically dependent on the actual type 
					//ADVANCED PROBABLY DO IN THE FUTURE: maybe allow them to pass in the "length" access function for each type
						//for now just support std::string 
					if constexpr (std::is_same_v<T, std::string>){
						std::memcpy(buf, x.size(), sizeof(std::string::size_type));
						buf += sizeof(std::string::size_type);
						std::memcpy(buf, x.data(), x.size());
						buf += sizeof(x.size());
					}
				}
			}(args), ...); //invoked lambda per element
		}, record.fields);
	}
	
	static void deserialize(const char& buf, RecordType<Args...>& record){
		std::apply([](auto&&... args){
			([](auto&& x){
				using T = std::decay_t<decltype(x)>;
				if constexpr (std::is_arithmetic_v<T>){
					std::memcpy(&x, buf, sizeof(T));
					buf += sizeof(T);
				}else{
					if constexpr (std::is_same_v<T, std::string>){
						//read in sizetype 
						std::string::size_type string_size; 
						std::memcpy(&string_size, buf, sizeof(string_size)); //maybe this should be a uint16_t or sth idk 
						buf += sizeof(string_size);
						//read in data and size
						x = std::string(buf, string_size);
						buf += string_size;
					}
				}
			}(args), ...);
		}, record.fields);
	}

};

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
	std::optional<RecordType> get(RecordType record); //could also be keytype 
    // range scan — returns all values where key is in [start, end]
    std::vector<RecordType> scan(KeyType start, KeyType end); //could also be recordtype 
private:
	splitChild(page_id_t parent_node, KeyType child); //could also be index 
		//take child, split into two. remember to add/update key stuff to parent node (strict min-key) 
	merge(page_id_t parent_node, KeyType left_child); //could also be index, could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
	redistribute(page_id_t parent_node, KeyType child); //could also be index 
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
	DiskManager* disk_manager_;
	//root page id is 0
};

#endif
