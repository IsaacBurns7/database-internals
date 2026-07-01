#ifndef INDEX_TREE

#define INDEX_TREE

#include "storage/disk_manager.h"
#include "common/config.h"
#include "index/key.h"
#include "type/schema.h"

//do I need schema poitner? perhaps I can just have a pagewriter/pagereader for writing onto disk later... 
//god jesus this is a complicated class
    //decompose into more classes later...

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

struct Breadcrumb {
    page_id_t page_id; 
    slot_id_t child_slot; 
};
using BTStack = std::vector<Breadcrumb>;
struct FindRecordMetadata{
    page_id_t leaf_page; //because our IOT implementation only stores data in leaf nodes 
    slot_id_t leaf_slot; //equals -1=2^16 if none is found... ? 
    BTStack bt_stack;
};

class BPlusTreeIterator;

class BPlusTree {
    //records are raw uint8_t* and interpreted via "Schema" interface
	//uses sibling pointers + strict min-key
		//will implement latch crabbing for concurrency
	//database overall is IoT (Index Organized Tables)

    //constructor needs:
        //schema_page_id for Schema*, b/c pagewriter and pagereader need it...
            //or it's given pagewriter and pagereader
        //root_page_id so it can walk for queries
        //disk manager class...
	bool insert(uint8_t* record, uint16_t len);
	bool remove(Key key);
	std::pair<uint8_t*, uint16_t> get(Key target);
    BPlusTreeIterator scan(Key start, Key end);
		// range scan — returns a cursor over [start, end], not a materialized list.
		// see BPlusTreeIterator: touching every leaf in the range up front would
		// require pinning them all simultaneously, which doesn't fit a bounded
		// buffer pool (POOL_SIZE in common/config.h).
private:
	friend class BPlusTreeIterator;

	Key extractKey(const uint8_t *record, uint16_t len) const;
    FindRecordMetadata findRecord(Key target);
        //use in insert(): needs BTStack
        //use in remove(): needs slot_id_x

	page_id_t splitChild(page_id_t parent_node, BTStack bt_stack, Key child); //returns new child
	void merge(page_id_t parent_node, BTStack bt_stack, Key left_child); //could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key)
	void redistribute(page_id_t parent_node, BTStack bt_stack, Key child);
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
 	DiskManager* disk_manager_;
	uint32_t root_page_id;
	TypeId key_type_id_ = TypeId::NUMERIC;
	uint8_t key_width_ = 8;
	const Schema* schema_;
	uint32_t key_col_idx_;
    //pagewriter
    //pagereader
};

/*
 * Lazily walks leaf slots in key order across sibling-linked leaf pages,
 * within [start_, end_]. Fetches/pins one leaf page at a time instead of
 * materializing the whole range up front (see BPlusTree::scan()).
 * Mirrors BPlusTree::get()'s per-slot decode via extractKey(), just repeated
 * slot-by-slot and across sibling pointers instead of a single lookup.
 * NOT YET IMPLEMENTED — declarations only.
 */
class BPlusTreeIterator {
public:
    BPlusTreeIterator(BPlusTree *tree, page_id_t start_page, slot_id_t start_slot, Key end): 
        tree_(tree), page_id_(start_page), slot_id_(start_slot), end_(end);

    // Returns the record at the cursor's current position and advances past
    // it, crossing into the right-sibling leaf when the current page is
    // exhausted. Returns std::nullopt once the extracted key exceeds end_,
    // or once there is no right sibling left to cross into.
    std::optional<std::pair<uint8_t*, uint16_t>> Next(){
        SlottedPage current_page = SlottedPage(data_);
        slot_id_t max_slot_id = current_page.getSlotCount();
        while(current_page.getRecord(slot_id_).second == 0){ //slot w/ length of 0 -> dead 
            //check end

            //go to right sibling(next page)
            if(slot_id_ == max_slot_id){
                page_id_ = current_page.getRightSibling();
                char *data; 
                tree_->disk_manager_->readPage(page_id_, data);
                data_ = data;
                current_page = SlottedPage(data);
                slot_id_ = -1; //ATTENTION ERROR PROBLEM: uhm will this wrap around back to 0 if we add 1 more ? :< 
                max_slot_id = current_page.getSlotCount(); 
            }
            slot_id_++; 
        }
        std::pair<const char*, uint16_t> slot_char = current_page.getRecord(slot_id_);
        slot_id_++;
        std::pair<uint8_t*, uint16_t> slot = std::move(std::pair<uint8_t*, uint16_t>{(uint8_t*)slot_char.first, slot_char.second}); //theres no fucking way that works...
            //no fucking shot
        auto ret = std::optional{std::move(slot)};
        return ret;//what the fuck am i doing...
    }

private:
	BPlusTree *tree_;
	page_id_t page_id_;
	slot_id_t slot_id_;
    Key end_;

    char *data_; //invariant is this is valid  
        //should this be a Page?
        //i dont really want to own this... maybe the buffer pool manager will own a list of pages? 
};

#endif
