#include "b_plus_tree.h"

/*
has access to:
    Key — compare, copy, free, serialize, deserialize. That's its entire interface with the type system.
	Schema - WHAT DO I NEED SCHEMA FOR? 
	- Schema.write(page_id_t node, uint16_t record_id, uint8_t* record);
	- Schema.read(page_id_t node, uint16_t record_id) -> uint8_t*;
	SlottedPage
	- read the pageheader 
		- if internal, 	
			- 0, 2, 4, ... -> page_id_t (child) 
			- 1, 3, 5, ... -> Key(uint8_t*),
		- if leaf, 
			- uint8_t* (record)
			- extract record via extractKey(uint8_t* record);
		- left/right sibling pointers (page_id_t) stored in pageheader 
    DiskManager - read, write
    - uint8_t* buf = disk.read(node.page_id);  // borrow for this scope
      // ... do work ... through slotted page
      disk.write(node.page_id, buf);           // or however your disk manager works
      // buf is dead after this point (because it will be evicted by the buffer pool manager)
	Schema class methods
	- const void* extractKey(const uint8_t* record);
	- void* extractKey(uint8_t* record);
	- int compareKeys(const void* keyA, const void* keyB);
	- below are fixed for a given schema because of overflow 
		- size_t keySize();
		- size_t recordSize();
	- void copyKey(void* dst, const void* src); 
		- not sure if I wanna allow varlen keys 
*/

bool BPlusTree::insert(uint8_t* record){
	//find correct leaf page and slot_id_x 
	//get page via disk manager 
	//read page as slottedpage 
	//if enough space to insert 
		//insert record at slot_id_x - you have to shift the rest of slots through memmove (cheap)
	//else 
		//split the current page, giving you a new page_id 
		//find if you should insert at this page or the new page, and then insert!! 
	//update keys in ancestral line via BTStack 
}
bool BPlusTree::remove(uint8_t* record){
	//find correct leaf page and slot_id_x 
	//get page via disk manager 
	//read page as slottedpage 
	//delete slot_id_x 
	//if this + sibling (via sibling pointer) can fit in one page, merge 
		//not so sure if this is a good idea??  
	//update keys in ancestral line via BTStack 
}
uint8_t* BPlusTree::get(Key target){
	//find correct leaf page and slot_id_x 
	//get page via disk manager 
	//read page as slottedpage 
	//return slot_id_x's record as uint8_t* 
}
std::vector<uint8_t*> BPlusTree::scan(Key start, Key end){
	//find correct start page and record 
	//find correct end page and record 
	//iterate from start to end via sibling pointers, scanning uint8_t* into vector
}

std::pair<page_id_t, uint16_t> BPlusTree::findRecord(Key Target){
	//start at root page 
	//loop until page is a leaf page
		//find first key "x" greater than or equal to record's key via binary search
		//go to the page_id directly after this key (strict min-key -> all records below strictly less than or equal to "x") 
			//this page_id must exist 
	//find first key "x" greater than or equal to the record's key via binary search with slot_id "slot_id_x"
	//return leaf page, slot_id_x, and BTStack 
}
//take child, split into two. 
//remember to add/update key stuff to parent node (strict min-key) 
void BPlusTree::splitChild(page_id_t parent, slot_id child){ //also needs BTStack
	//allocate new page 
	//pick a pivot record in the child - the midpoint is a good heuristic
	//make right sibling of child newly allocated page 
		//separator key is pivot record's key 
		//insert key + page_id in parent node slots, right after child
			//if not enough space, splitChild(parent's parent, parent) via BTStack?? WTF!!
				//ok so we need to maintain a BTStack
	//move [pivot, end] into new right sibling - [begin, pivot) still in OG child 
}
//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
void BPlusTree::merge(page_id_t parent_node, Key left_child){ 
	//find right child via sibling pointer 
	//delete right child and separator key from parent 
	//move all of right child's live slots into left child 
}
	//not doing for now 
	// void redistribute(page_id_t parent_node, Key child);  
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
/*
	DiskManager* disk_manager_;
	// uint16_t primary_key_index;
	uint32_t root_page_id; 
	uint32_t schema_page_id;
	Schema schema; 
*/

#endif
