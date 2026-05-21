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

/*
has access to:
    Key — compare, copy, free, serialize, deserialize. That's its entire interface with the type system.
	Schema - WHAT DO I NEED SCHEMA FOR? 
	- Schema.write(page_id_t node, uint16_t record_id, uint8_t* record);
	- Schema.read(page_id_t node, uint16_t record_id) -> uint8_t*;
	SlottedPage
	- read the pageheader 
		- if internal, 	
			- 0 -> Key(uint8_t*),
			- 1 -> page_id_t (child)
			- 2 -> page_id_t (sibling pointer)
		- if leaf, 
			- 0 -> Key(uint8_t*),
			- 1 -> uint8_t* (record)
			- 2 -> page_id_t (sibling pointer)
    DiskManager - read, write
    - uint8_t* buf = disk.read(node.page_id);  // borrow for this scope
      // ... do work ... through slotted page
      disk.write(node.page_id, buf);           // or however your disk manager works
      // buf is dead after this point (because it will be evicted by the buffer pool manager)
*/

class BPlusTree { 
    //records are raw uint8_t* and interpreted via "Schema" interface 
	//uses sibling pointers + strict min-key 
		//will implement latch crabbing for concurrency
	//database overall is IoT (Index Organized Tables)
	//records stored as uint8_t*, interpreted via "schema" interface
	bool insert(uint8_t* record);
	bool remove(uint8_t* record); 
	uint8_t* get(Key target); 
    std::vector<uint8_t*> scan(Key start, Key end); 
		// range scan — returns all values where key is in [start, end]
private:
	void splitChild(page_id_t parent_node, Key child); 
		//take child, split into two. 
		//remember to add/update key stuff to parent node (strict min-key) 
	void merge(page_id_t parent_node, Key left_child); //could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
	void redistribute(page_id_t parent_node, Key child);  
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
	DiskManager* disk_manager_;
	// uint16_t primary_key_index;
	uint32_t root_page_id; 
	uint32_t schema_page_id;
	Schema schema; 
};

#endif
