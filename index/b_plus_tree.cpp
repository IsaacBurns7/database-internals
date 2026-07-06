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

// Assumes `record` is a schema-shaped buffer where the key column sits at
// schema_->GetColumn(key_col_idx_).GetOffset() — true for leaf slots (full
// serialized tuples). Internal-node key slots store the key alone with no
// preceding columns, so this is only correct for them when key_col_idx_ == 0.
Key BPlusTree::extractKey(const uint8_t *record, uint16_t len) const {
	if (record == nullptr || len == 0) {
		return Key::FromBytes(key_type_id_, key_width_, reinterpret_cast<const uint8_t *>(""), 0);
	}

	const uint32_t key_offset = schema_->GetColumn(key_col_idx_).GetOffset();
	const uint16_t offset = key_offset <= len ? static_cast<uint16_t>(key_offset) : len;
	const uint8_t *key_start = record + offset;
	const uint16_t remaining = static_cast<uint16_t>(len - offset);

    //test this later, for now just dont fucking use varchar as a key...
	if (key_type_id_ == TypeId::VARCHAR) {
		if (remaining < sizeof(uint16_t)) {
			return Key::FromBytes(key_type_id_, key_width_, key_start, remaining);
		}
		uint16_t varchar_len = 0;
		std::memcpy(&varchar_len, key_start, sizeof(uint16_t));
		const uint16_t key_len = static_cast<uint16_t>(sizeof(uint16_t) + varchar_len);
		const uint16_t clamped_len = key_len <= remaining ? key_len : remaining;
		return Key::FromBytes(key_type_id_, key_width_, key_start, clamped_len);
	}

	const uint16_t key_len = key_width_ <= remaining ? key_width_ : remaining;
	return Key::FromBytes(key_type_id_, key_width_, key_start, key_len);
}

//wait what about dead/live slots in the slottedpage?
    //dead slots shouldn't exist in internal nodes... ?
//oh shoot i forgot about the btstack...

/*
 * ARCHIVED — findRecordLinear(): the pre-binary-search implementation, kept
 * only for reference. Superseded by the binary-search findRecord() below
 * once SlottedPage::insertRecordAt() (see analysis.md Appendix 1) made
 * physical slot order == key order, which is what a real binary search
 * requires. Not compiled — this whole block is a comment.
 *
FindRecordMetadata BPlusTree::findRecordLinear(Key target){
    FindRecordMetadata ret{};
    BTStack bt_stack{};
	char* current_page_data = new char[PAGE_SIZE];
	disk_manager_->readPage(root_page_id, current_page_data);
	page_id_t current_page_id = root_page_id;

    // --- Path back to binary search (not implemented yet — linear scan below instead) ---
    // 1. HOW: binary search over a page needs slot POSITION to equal KEY order at all
    //    times. You can't get that by physically reordering the Slot[] array on insert,
    //    because that renumbers other entries' slot_ids — which SlottedPage promises
    //    never to do, for internal AND leaf pages alike (BTStack breadcrumbs and
    //    slot_id-as-row-handle both depend on that promise). Instead: maintain a
    //    SEPARATE small array of slot_ids in sorted-by-key order (a "logical order"
    //    index) distinct from physical slot identity. Binary search walks THIS array;
    //    each step still says "look at slot_id X," and X's physical position/identity
    //    never changes. Only the logical-order array gets reordered on insert — that's
    //    search metadata, not the records or their slot_ids.
    // 2. WHAT NEEDS TO CHANGE IN SlottedPage, AND WHY: insertRecord() would need a
    //    variant that takes a comparator (or a target sorted position) and inserts the
    //    new slot_id into that logical-order array at the right spot — a memmove of
    //    slot_id-sized entries only, not the record heap or the Slot[] array itself, so
    //    still cheap. deleteRecord() would need to remove the tombstoned slot_id from
    //    that same logical-order array (the Slot[] entry stays a tombstone, per the
    //    existing invariant — only its entry in the order array goes away). Without
    //    this, there's no sorted structure to binary search over, which is exactly why
    //    the walk below is a linear scan instead.
	while (true) {
		SlottedPage current_page(current_page_data);
		if (current_page.getPageType() == SlottedPageType::LEAF_PAGE) break;

		// BE AWARE OF SHIFTS: this used to be a binary search assuming slot order ==
		// key order. SlottedPage::insertRecord() always appends at slot_count and
		// never reorders existing slots (see insert()'s call to leaf.insertRecord,
		// which is the root cause — it appends in insertion order, not key order).
		// So slot position here was never actually guaranteed sorted by key, for
		// either page type. Linear scan for now — see the binary-search plan above.
		slot_id_t n = current_page.getSlotCount();
		slot_id_t best_slot = n;  // slot of the smallest key >= target seen so far
		slot_id_t max_slot = n;   // slot of the largest key seen so far (fallback)
		bool has_best = false, has_max = false;
		Key best_key = target;
		Key max_key = target;

		for (slot_id_t slot = 0; slot < n; slot = static_cast<slot_id_t>(slot + 2)) {
			auto [bytes, len] = current_page.getRecord(slot);
			if (len == 0) continue;  // tombstoned key slot (shouldn't happen in internal nodes, but be safe)
			Key key = extractKey(reinterpret_cast<const uint8_t*>(bytes), len);

			if (key.Compare(target) >= 0 && (!has_best || key.Compare(best_key) < 0)) {
				best_key = key;
				best_slot = slot;
				has_best = true;
			}
			if (!has_max || key.Compare(max_key) > 0) {
				max_key = key;
				max_slot = slot;
				has_max = true;
			}
		}

		// page_id follows the chosen key slot at +1.
		// If all keys < target, descend into the child with the largest key present
		// (the old "rightmost slot" fallback assumed sorted position == largest key;
		// under unsorted order we have to find that child by value instead).
		slot_id_t page_slot = has_best ? static_cast<slot_id_t>(best_slot + 1)
		                                : static_cast<slot_id_t>(max_slot + 1);
		auto [page_bytes, page_len] = current_page.getRecord(page_slot);
		page_id_t child_page_id;
		std::memcpy(&child_page_id, page_bytes, sizeof(page_id_t));
        bt_stack.push_back({current_page_id, page_slot});

		disk_manager_->readPage(child_page_id, current_page_data);
        current_page_id = child_page_id;

    }

	// Leaf binary search: every slot is a raw serialized tuple. extractKey()
	// reads the key bytes directly at schema_->GetColumn(key_col_idx_).GetOffset()
	// inside the record — no full Tuple::Deserialize, no heap allocations.
	// Caveat: Column::GetOffset() only matches the serialized layout when no
	// variable-length column precedes key_col_idx_ (Tuple::Serialize writes columns
	// sequentially with no fixed/variable split yet, and no null bitmap prefix).

    // BE AWARE OF SHIFTS: same reasoning as the internal-node walk above — leaf.
    // insertRecord() (called unconditionally in insert()) also only appends at
    // slot_count, in insertion order, so slot position here was never guaranteed to
    // track key order either. Linear scan for now — see the binary-search plan above.
	SlottedPage leaf(current_page_data);
	slot_id_t n = leaf.getSlotCount();
	slot_id_t best_slot = n;  // n means "no live slot with key >= target"
	bool has_best = false;
	Key best_key = target;

	for (slot_id_t slot = 0; slot < n; ++slot) {
		auto [bytes, len] = leaf.getRecord(slot);
		if (len == 0) continue;  // tombstoned slot
		Key key = extractKey(reinterpret_cast<const uint8_t*>(bytes), len);
		if (key.Compare(target) >= 0 && (!has_best || key.Compare(best_key) < 0)) {
			best_key = key;
			best_slot = slot;
			has_best = true;
		}
	}

	// l is the first leaf slot where key >= target (or n if none is live and in range).
	slot_id_t l = best_slot;
    ret.leaf_page = current_page_id;
    ret.leaf_slot = l;
    ret.bt_stack = std::move(bt_stack);
    delete[] current_page_data;
    return ret;
}
 *
 * END ARCHIVED findRecordLinear()
 */

//returns first leaf slot where key >= target
FindRecordMetadata BPlusTree::findRecord(Key target){
    FindRecordMetadata ret{};
    BTStack bt_stack{};
	char* current_page_data = new char[PAGE_SIZE];
	disk_manager_->readPage(root_page_id, current_page_data);
	page_id_t current_page_id = root_page_id;

	while (true) {
		SlottedPage current_page(current_page_data);
		if (current_page.getPageType() == SlottedPageType::LEAF_PAGE) break;

		// Binary search over key-slots. Internal-node layout is pairs:
		// slot 2*j holds key j, slot 2*j+1 holds the child page_id that
		// follows it. Valid now that inserts place separator keys at their
		// sorted position instead of appending (SlottedPage::insertRecordAt,
		// see analysis.md Appendix 1) — physical slot order == key order.
		// Internal-node key slots aren't expected to be tombstoned (there's
		// no internal-node deletion path yet), so unlike the leaf search
		// below, no dead-slot handling here.
		slot_id_t n = current_page.getSlotCount();
		slot_id_t num_keys = static_cast<slot_id_t>(n / 2);
		slot_id_t lo = 0, hi = num_keys;
		while (lo < hi) {
			slot_id_t mid = static_cast<slot_id_t>(lo + (hi - lo) / 2);
			auto [bytes, len] = current_page.getRecord(static_cast<slot_id_t>(mid * 2));
			Key key = extractKey(reinterpret_cast<const uint8_t*>(bytes), len);
			if (key.Compare(target) < 0) lo = static_cast<slot_id_t>(mid + 1);
			else hi = mid;
		}

		// lo is the index of the first key >= target, or num_keys if none is.
		// If none is, descend into the rightmost child — mirrors the old
		// linear scan's "largest key seen" fallback, which under sorted
		// order is simply the last key slot's child pointer.
		slot_id_t page_slot = (lo < num_keys)
		    ? static_cast<slot_id_t>(lo * 2 + 1)
		    : static_cast<slot_id_t>(n - 1);

		auto [page_bytes, page_len] = current_page.getRecord(page_slot);
		page_id_t child_page_id;
		std::memcpy(&child_page_id, page_bytes, sizeof(page_id_t));
        bt_stack.push_back({current_page_id, page_slot});

		disk_manager_->readPage(child_page_id, current_page_data);
        current_page_id = child_page_id;

    }

	// Leaf binary search: every slot is a raw serialized tuple. extractKey()
	// reads the key bytes directly at schema_->GetColumn(key_col_idx_).GetOffset()
	// inside the record — no full Tuple::Deserialize, no heap allocations.
	// Caveat: Column::GetOffset() only matches the serialized layout when no
	// variable-length column precedes key_col_idx_ (Tuple::Serialize writes columns
	// sequentially with no fixed/variable split yet, and no null bitmap prefix).
	//
	// Unlike the internal-node search above, leaf slots CAN be tombstoned —
	// remove() calls deleteRecord(), which zeroes Slot.offset without
	// shifting the array (analysis.md Appendix 1 scopes the fix to ordered
	// insert only; delete stays tombstone-based). A tombstoned slot has no
	// recoverable key, so a textbook binary search can't compare against it
	// directly. When the probed slot is dead, scan forward for the nearest
	// live slot to stand in for it: if one exists inside the current
	// [lo, hi) bracket, use its position/key to decide the branch exactly as
	// the vanilla binary search would; if the whole remaining bracket is
	// dead, shrink hi to mid (the answer must be to the left). This stays
	// close to O(log n) in the common case, degrading only near long
	// tombstone runs.
	SlottedPage leaf(current_page_data);
	slot_id_t n = leaf.getSlotCount();
	slot_id_t lo = 0, hi = n;
	while (lo < hi) {
		slot_id_t mid = static_cast<slot_id_t>(lo + (hi - lo) / 2);
		slot_id_t probe = mid;
		auto rec = leaf.getRecord(probe);
		while (rec.second == 0 && probe < hi) {
			probe = static_cast<slot_id_t>(probe + 1);
			rec = leaf.getRecord(probe);
		}
		if (probe >= hi) {
			hi = mid;  // [mid, hi) is entirely dead — answer lies left of mid
			continue;
		}
		Key key = extractKey(reinterpret_cast<const uint8_t*>(rec.first), rec.second);
		if (key.Compare(target) < 0) lo = static_cast<slot_id_t>(probe + 1);
		else hi = probe;
	}

	// l is the first leaf slot where key >= target (or n if none is live and in range).
	slot_id_t l = lo;
    ret.leaf_page = current_page_id;
    ret.leaf_slot = l;
    ret.bt_stack = std::move(bt_stack);
    delete[] current_page_data;
    return ret;
}

bool BPlusTree::insert(uint8_t* record, uint16_t len){
	Key key = extractKey(record, len);
	FindRecordMetadata find_record_metadata = findRecord(key);
    char* page_data = new char[PAGE_SIZE];
    disk_manager_->readPage(find_record_metadata.leaf_page, page_data);
    SlottedPage leaf(page_data);
    // insert at the sorted position findRecord() already computed —
    // find_record_metadata.leaf_slot is lower_bound(key) within this leaf,
    // which is exactly where this record belongs among its siblings.
    // insertRecordAt() shifts the Slot[] array to open a gap there (see
    // analysis.md Appendix 1), instead of appending at slot_count like the
    // old insertRecord() call this replaces.
    //
    // No external getFreeSpace()/compactify() pre-check here on purpose:
    // insertRecordAt() already tries compaction internally (and correctly
    // re-translates leaf_slot across it — see slotted_page.cpp) before
    // giving up. Pre-compacting out here would invalidate leaf_slot before
    // insertRecordAt() ever saw it, since it was computed by findRecord()
    // against the pre-compaction layout. A nullopt back from insertRecordAt()
    // means "won't fit even after compaction" — exactly the split case.
    std::optional<slot_id_t> first_available_slot =
        leaf.insertRecordAt(find_record_metadata.leaf_slot, (const char*)record, len); //page dirty
    if(!first_available_slot.has_value()){
        page_id_t new_child = splitChild(find_record_metadata.bt_stack, key); //what does it need key for??
            //updates keys according to strict min-key
        //find if you should insert at this page or the new page, and then insert!!
            //look at bt_stack for parent, use slot_id to check if its dead... ? im not quite sure...
    }
    //update via diskmanager (no bufferpoolmanager yet... DISGUSTING!!!)
    //when would this return false?
    delete[] page_data;
	return true;
}

//returns whether or not it successfully deleted
bool BPlusTree::remove(Key key){
//this can be a function (internal)
	FindRecordMetadata find_record_metadata = findRecord(key);
    char* page_data = new char[PAGE_SIZE];
    disk_manager_->readPage(find_record_metadata.leaf_page, page_data);
    SlottedPage leaf(page_data);

//this can be a function (internal)
    auto [bytes,len] = leaf.getRecord(find_record_metadata.leaf_slot);
    Key found_key = extractKey((const uint8_t*)bytes, len);
    if(key.Compare(found_key)){ //if keys are equal, returns 1, -1 -> coalesces to true
        delete[] page_data;
        return false;
    }
    leaf.deleteRecord(find_record_metadata.leaf_slot);
    //if this + right sibling (via sibling pointer) can fit in one page, merge
		//not so sure if this is a good idea??
        //PLUS!!!! merge updates ancestral keys via BTStack
    //update via diskmanager
    delete[] page_data;
	return true;
}

//returns record that matches target. Caller owns the returned buffer and
//must delete[] it — page_data is freed before this function returns, so the
//record can't be handed back as a pointer aliasing it (unlike the iterator,
//which keeps its page buffer alive for its whole lifetime).
std::pair<uint8_t*, uint16_t> BPlusTree::get(Key target){
 //this can be a function (internal)
	FindRecordMetadata find_record_metadata = findRecord(target);
    char* page_data = new char[PAGE_SIZE];
    disk_manager_->readPage(find_record_metadata.leaf_page, page_data);
    SlottedPage leaf(page_data);

//this can be a function (internal)
    auto [bytes,len] = leaf.getRecord(find_record_metadata.leaf_slot);
    Key found_key = extractKey((const uint8_t*)bytes, len);
    if(target.Compare(found_key)){ //if keys are equal, returns 1, -1 -> coalesces to true
        delete[] page_data;
        return {nullptr, 0};
    }

    uint8_t* record_copy = new uint8_t[len];
    std::memcpy(record_copy, bytes, len);
    delete[] page_data;
    return {record_copy, len};
}

//returns iterator that goes from start to end via Next() 
BPlusTreeIterator BPlusTree::scan(Key start, Key end){
    FindRecordMetadata start_data = findRecord(start);
    BPlusTreeIterator iterator{this, start_data.leaf_page, start_data.leaf_slot, end};
	return iterator; 
}


//take child, split into two.
//remember to add/update key stuff to parent node (strict min-key) via BTStack
//by god does this need to be simpler...
//
// KEY PROMOTION vs KEY COPY-UP, and WHY they differ between leaf and internal splits:
//   - Leaf split: the pivot's key is COPIED up into the parent as the new separator.
//     The leaf keeps its own copy too, because leaf slots are real data (this is an
//     IOT — the leaf IS the row), and that row still needs to exist and be found by
//     its own key after the split. Nothing is "spent" by using it as a separator.
//   - Internal split: the middle key is PROMOTED, not copied — it moves up into the
//     grandparent and does NOT remain in either resulting internal node. Internal
//     keys aren't data, they're pure routing information: under strict min-key, a
//     key at slot i asserts "everything reachable through the following child
//     pointer has key >= this value." If that key were left behind in a child after
//     also being promoted, both the parent and the child would claim to be the
//     authoritative min-key for the same subtree pointer — and after the split, the
//     child pointer that key used to gate no longer even lives in that node. So for
//     internal nodes the key has to be moved, not duplicated, to keep the invariant
//     single-sourced. (This is also why splitChild probably wants to become
//     splitLeaf/splitInternal rather than one function branching on page type.)
page_id_t BPlusTree::splitChild(BTStack bt_stack, Key child){
    FindRecordMetadata child_metadata = findRecord(child);
    char* new_page_data = new char[PAGE_SIZE];
    char* child_page_data = new char[PAGE_SIZE];
    page_id_t fresh_page_id = disk_manager_->allocatePage();
    disk_manager_->readPage(fresh_page_id, new_page_data);
    disk_manager_->readPage(child_metadata.leaf_page, child_page_data);
	//pick a pivot record in the child - the midpoint is a good heuristic
        //options: 
            //pick midpoint and walk forward till you hit a live slot
            //any others?
    SlottedPage child_page(child_page_data);
    slot_id_t slot_count = child_page.getSlotCount();
    slot_id_t pivot = slot_count / 2;
    auto rec = child_page.getRecord(pivot);
    while(rec.second == 0){ //rec.second is len
        pivot++; 
        rec = child_page.getRecord(pivot);
    }
	//make right sibling of child newly allocated page 
    page_id_t right_sibling_id = child_page.getRightSibling();
    char* right_sibling_data = new char[PAGE_SIZE];
    SlottedPage right_sibling{right_sibling_data};
    child_page.setRightSibling(fresh_page_id);
    right_sibling.setLeftSibling(fresh_page_id);

    //move [pivot, end] into new right sibling
        //TODO: Modify SlottedPage to allow bulk-move 
    SlottedPage new_page(new_page_data);
    new_page.init(fresh_page_id, SlottedPageType::LEAF_PAGE);
    for(slot_id_t slot = pivot; slot < slot_count; ++slot){
        auto [bytes, len] = child_page.getRecord(slot);
        if(len == 0) continue;
        new_page.insertRecord(bytes, len);
        child_page.deleteRecord(slot);
    }
    child_page.compactify();

    for(size_t i = bt_stack.size()-1; i > 0; --i){
        auto [parent_page_id, child_slot] = bt_stack[i];
        //if not enough space
            //splitInternal(parent) -> this shouldnt update... ? 
        //insert key + page_id in parent node slots, first dead slot right after child 
        //oh my god update... wtf...   
    }

    delete[] new_page_data;
    delete[] child_page_data;
    delete[] right_sibling_data;
}

page_id_t BPlusTree::splitInternal(BTStack bt_stack, Key internal){
    FindRecordMetadata internal_node_metadata = findRecord(internal);
    
}

//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
void BPlusTree::merge(page_id_t parent_node, BTStack bt_stack, Key left_child){ 
	(void)parent_node;
	(void)left_child;
	//find right child via sibling pointer 
	//delete right child and separator key from parent 
	//move all of right child's live slots into left child 
}

    void BPlusTree::redistribute(page_id_t parent_node, BTStack bt_stack, Key child){
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
