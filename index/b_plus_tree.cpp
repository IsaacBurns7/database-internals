#include "b_plus_tree.h"
#include "slotted_page.h"
#include "type/schema.h"

#include <cstring>

/*
has access to:
    Key — compare, copy, free, serialize, deserialize. That's its entire interface with the type system.
	Schema - WHAT DO I NEED SCHEMA FOR? 
	- Schema.write(page_id_t node, uint16_t record_id, uint8_t* record);
	- Schema.read(page_id_t node, uint16_t record_id) -> uint8_t*;
	SlottedPage
	- read the pageheader 
		- if internal,
			- 0, 2, 4, ... -> Key(uint8_t*)
			- 1, 3, 5, ... -> page_id_t (child)
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

Key BPlusTree::extractKey(const uint8_t *record, uint16_t len) const {
	if (record == nullptr || len == 0) {
		return Key::FromBytes(key_type_id_, key_width_, reinterpret_cast<const uint8_t *>(""), 0);
	}

    //test this later, for now just dont fucking use varchar as a key... 
	if (key_type_id_ == TypeId::VARCHAR) {
		if (len < sizeof(uint16_t)) {
			return Key::FromBytes(key_type_id_, key_width_, record, len);
		}
		uint16_t varchar_len = 0;
		std::memcpy(&varchar_len, record, sizeof(uint16_t));
		const uint16_t key_len = static_cast<uint16_t>(sizeof(uint16_t) + varchar_len);
		const uint16_t clamped_len = key_len <= len ? key_len : len;
		return Key::FromBytes(key_type_id_, key_width_, record, clamped_len);
	}

	const uint16_t key_len = key_width_ <= len ? key_width_ : len;
	return Key::FromBytes(key_type_id_, key_width_, record, key_len);
}

//wait what about dead/live slots in the slottedpage?
    //dead slots shouldn't exist in internal nodes... ?
//oh shoot i forgot about the btstack...
std::pair<page_id_t, uint16_t> BPlusTree::findRecord(Key target){
	char* current_page_data;
	disk_manager_->readPage(root_page_id, current_page_data);
	page_id_t current_page_id = root_page_id;

	while (true) {
		SlottedPage current_page(current_page_data);
		if (current_page.getPageType() == SlottedPageType::LEAF_PAGE) break;

		// Binary search over even slots (keys). Layout: key/page_id/key/page_id...
		// Even slots are keys; odd slots are page_ids.
		slot_id_t n = current_page.getSlotCount();
		slot_id_t l = 0, r = n;

		while (l < r) {
			slot_id_t mid = (l + r) / 2;
			mid &= ~static_cast<slot_id_t>(1);  // ensure mid is always even (key slot)

			auto [bytes, len] = current_page.getRecord(mid);
			Key mid_key = Key::FromBytes(key_type_id_, key_width_,
			                             reinterpret_cast<const uint8_t*>(bytes), len);

			if (mid_key.Compare(target) < 0) {
				l = static_cast<slot_id_t>(mid + 2);
			} else {
				r = mid;
			}
		}

		// l is the first even slot where key >= target; page_id follows at l+1.
		// If all keys < target, descend into the rightmost child (last slot, n-1).
		slot_id_t page_slot = (l < n) ? static_cast<slot_id_t>(l + 1)
		                               : static_cast<slot_id_t>(n - 1);
		auto [page_bytes, page_len] = current_page.getRecord(page_slot);
		page_id_t child_page_id;
		std::memcpy(&child_page_id, page_bytes, sizeof(page_id_t));

		disk_manager_->readPage(child_page_id, current_page_data);
		current_page_id = child_page_id;
	}

	// Leaf binary search: every slot is a raw serialized tuple.
	// We read the key bytes directly at schema_->GetColumn(key_col_idx_).GetOffset()
	// inside the record — no full Tuple::Deserialize, no heap allocations.
	// Caveat: Column::GetOffset() only matches the serialized layout when no
	// variable-length column precedes key_col_idx_ (Tuple::Serialize writes columns
	// sequentially with no fixed/variable split yet, and no null bitmap prefix).
	SlottedPage leaf(current_page_data);
	slot_id_t n = leaf.getSlotCount();
	slot_id_t l = 0, r = n;
	uint32_t key_offset = schema_->GetColumn(key_col_idx_).GetOffset();

	while (l < r) {
		slot_id_t mid = (l + r) / 2;

		auto [bytes, len] = leaf.getRecord(mid);
		Key mid_key = Key::FromBytes(key_type_id_, key_width_,
		                             reinterpret_cast<const uint8_t*>(bytes) + key_offset,
		                             key_width_);

		if (mid_key.Compare(target) < 0) {
			l = static_cast<slot_id_t>(mid + 1);
		} else {
			r = mid;
		}
	}

	// l is the first leaf slot where key >= target.
	return {current_page_id, l};
}

bool BPlusTree::insert(uint8_t* record, uint16_t len){
	Key key = extractKey(record, len);
	(void)key;
	//find correct leaf page and slot_id_x 
	//get page via disk manager 
	//read page as slottedpage 
	//if enough space to insert 
		//insert record at slot_id_x - you have to shift the rest of slots through memmove (cheap)
	//else 
		//split the current page, giving you a new page_id 
		//find if you should insert at this page or the new page, and then insert!! 
	//update keys in ancestral line via BTStack 
	return false;
}
bool BPlusTree::remove(uint8_t* record, uint16_t len){
	Key key = extractKey(record, len);
	(void)key;
	//find correct leaf page and slot_id_x 
	//get page via disk manager 
	//read page as slottedpage 
	//delete slot_id_x 
	//if this + sibling (via sibling pointer) can fit in one page, merge 
		//not so sure if this is a good idea??  
	//update keys in ancestral line via BTStack 
	return false;
}
uint8_t* BPlusTree::get(Key target){
	(void)target;
	//find correct leaf page and slot_id_x 
	//get page via disk manager 
	//read page as slottedpage 
	//return slot_id_x's record as uint8_t* 
	return nullptr;
}
std::vector<uint8_t*> BPlusTree::scan(Key start, Key end){
	(void)start;
	(void)end;
	//find correct start page and record 
	//find correct end page and record 
	//iterate from start to end via sibling pointers, scanning uint8_t* into vector
	return {};
}


//take child, split into two. 
//remember to add/update key stuff to parent node (strict min-key) 
void BPlusTree::splitChild(page_id_t parent_node, Key child){ //also needs BTStack
	(void)parent_node;
	(void)child;
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
	(void)parent_node;
	(void)left_child;
	//find right child via sibling pointer 
	//delete right child and separator key from parent 
	//move all of right child's live slots into left child 
}

	void BPlusTree::redistribute(page_id_t parent_node, Key child){
		(void)parent_node;
		(void)child;
	}
	//not doing for now 
	// void redistribute(page_id_t parent_node, Key child);  
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
/*
	DiskManager* disk_manager_;
	uint32_t root_page_id; 
    pagewriter
    pagereader
*/
