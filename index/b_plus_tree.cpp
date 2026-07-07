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

/*
Extracts the key utilizing internal 
*/
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
		//
		// Strict min-key means the correct child for `target` is the one
		// whose key is the LARGEST key <= target (the floor/predecessor),
		// not the smallest key >= target (a plain lower_bound) — those only
		// coincide on an exact match. So this binary-searches an upper_bound
		// instead: lo ends up as the first index whose key is strictly
		// greater than target (or num_keys if every key is <= target), and
		// the floor child is therefore at lo - 1.
		slot_id_t n = current_page.getSlotCount();
		slot_id_t num_keys = static_cast<slot_id_t>(n / 2);
		slot_id_t lo = 0, hi = num_keys;
		while (lo < hi) {
			slot_id_t mid = static_cast<slot_id_t>(lo + (hi - lo) / 2);
			auto [bytes, len] = current_page.getRecord(static_cast<slot_id_t>(mid * 2));
			Key key = extractKey(reinterpret_cast<const uint8_t*>(bytes), len);
			if (key.Compare(target) <= 0) lo = static_cast<slot_id_t>(mid + 1);
			else hi = mid;
		}

		// lo == 0 means even the first key is > target (target smaller than
		// everything in this subtree) — there's no valid floor, so fall back
		// to child 0, the closest thing to a "smallest" child available.
		// Otherwise the floor child is at index lo - 1.
		slot_id_t floor_idx = (lo > 0) ? static_cast<slot_id_t>(lo - 1) : 0;
		slot_id_t page_slot = static_cast<slot_id_t>(floor_idx * 2 + 1);

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
	//
	// `best` tracks the closest live match found so far, separately from
	// lo/hi. The forward scan can land probe on a live slot that sits
	// exactly at the current hi (a tombstone run ending right at the
	// boundary) — checking `probe >= hi` there would wrongly treat that as
	// "nothing live in range" and discard a real match. Checking rec.second
	// (whether the scan actually stopped on a live slot, vs ran out of
	// slots) fixes that, but then shrinking hi to probe stops making
	// progress toward lo whenever probe keeps re-resolving to the same
	// slot — so the branch shrinks hi to mid (mid is what guarantees lo/hi
	// converge) and stashes probe in best instead of relying on lo/hi alone
	// to carry the answer out of the loop.
	SlottedPage leaf(current_page_data);
	slot_id_t n = leaf.getSlotCount();
	slot_id_t lo = 0, hi = n;
	slot_id_t best = n;  // n means "no live match found yet"
	while (lo < hi) {
		slot_id_t mid = static_cast<slot_id_t>(lo + (hi - lo) / 2);
		slot_id_t probe = mid;
		auto rec = leaf.getRecord(probe);
		while (rec.second == 0 && probe < hi) {
			probe = static_cast<slot_id_t>(probe + 1);
			rec = leaf.getRecord(probe);
		}
		if (rec.second == 0) {
			hi = mid;  // [mid, hi) is entirely dead — answer lies left of mid
			continue;
		}
		Key key = extractKey(reinterpret_cast<const uint8_t*>(rec.first), rec.second);
		if (key.Compare(target) < 0) {
			lo = static_cast<slot_id_t>(probe + 1);
		} else {
			best = probe;
			hi = mid;
		}
	}

	// l is the first live leaf slot where key >= target (or n if none is live and in range).
	slot_id_t l = (best < n) ? best : lo;
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
        (void)new_child;
    } else {
        disk_manager_->writePage(find_record_metadata.leaf_page, page_data);
    }
    delete[] page_data;
	return true; //when would this return false?
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
    bool ret = leaf.deleteRecord(find_record_metadata.leaf_slot);
    if(ret){
        disk_manager_->writePage(find_record_metadata.leaf_page, page_data);
    }
    //LATER
        //if this + right sibling (via sibling pointer) can fit in one page, merge
            //not so sure if this is a good idea??
            //PLUS!!!! merge updates ancestral keys via BTStack
    delete[] page_data;
	return ret;
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
    // fresh_page_id was just allocated — DiskManager doesn't extend the file
    // until the first write to a page, so reading it back here would throw
    // "Read past EOF". new_page is fully built via init() below anyway, so
    // its buffer starts as plain heap memory instead, same as splitRoot()'s
    // new_root_data.
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
    child_page.setRightSibling(fresh_page_id);

    //move [pivot, end] into new right sibling
        //TODO: Modify SlottedPage to allow bulk-move
    SlottedPage new_page(new_page_data);
    new_page.init(fresh_page_id, SlottedPageType::LEAF_PAGE);
    new_page.setLeftSibling(child_metadata.leaf_page);
    new_page.setRightSibling(right_sibling_id);
    for(slot_id_t slot = pivot; slot < slot_count; ++slot){
        auto [bytes, len] = child_page.getRecord(slot);
        if(len == 0) continue;
        new_page.insertRecord(bytes, len);
        child_page.deleteRecord(slot);
    }
    child_page.compactify();

    // Deriving the new separator: the pivot's key was COPIED into new_page
    // (see the KEY PROMOTION vs KEY COPY-UP comment above), so a stable copy
    // of it lives at new_page slot 0 — read it from there instead of from
    // child_page, whose copy of it is about to be reclaimed by the
    // compactify() call below (it was tombstoned by the move loop).
    auto [pivot_bytes, pivot_len] = new_page.getRecord(0);
    Key pivot_key = extractKey(reinterpret_cast<const uint8_t*>(pivot_bytes), pivot_len);

    // Persist child_page and new_page before splitRoot()/insertIntoParent()
    // run — splitRoot() re-reads child_metadata.leaf_page (as left_child_id)
    // to derive the left subtree's min-key, so that page must already
    // reflect this split on disk by the time it's called.
    disk_manager_->writePage(child_metadata.leaf_page, child_page_data);
    disk_manager_->writePage(fresh_page_id, new_page_data);

    // The old right sibling (if any) now has fresh_page_id as its left
    // neighbor instead of child_metadata.leaf_page — read-modify-write it
    // rather than blindly overwriting, since it's an existing page whose
    // record contents must be preserved.
    if(right_sibling_id != INVALID_PAGE_ID){
        char* right_sibling_data = new char[PAGE_SIZE];
        disk_manager_->readPage(right_sibling_id, right_sibling_data);
        SlottedPage right_sibling(right_sibling_data);
        right_sibling.setLeftSibling(fresh_page_id);
        disk_manager_->writePage(right_sibling_id, right_sibling_data);
        delete[] right_sibling_data;
    }

    if(bt_stack.empty()){
        // child_metadata.leaf_page had no parent — it WAS the root. Build a
        // fresh internal root over {it, fresh_page_id} instead of inserting
        // a separator into a parent that doesn't exist. See splitRoot().
        splitRoot(child_metadata.leaf_page, pivot_key, fresh_page_id);
    } else {
        insertIntoParent(bt_stack, pivot_key, fresh_page_id);
    }

    delete[] new_page_data;
    delete[] child_page_data;
    return fresh_page_id;
}



// Splits an internal node, mirroring splitChild()'s shape:
//   - splitChild locates its leaf via findRecord(child) because findRecord()
//     naturally walks down TO a leaf. That trick doesn't work here — findRecord()
//     always descends past internal nodes, it can never stop at one. The node
//     being split is already known without it: it's the one the caller is
//     splitting because inserting into it failed, i.e. bt_stack.back().page_id.
//     So `internal` goes unused, same as `child` in splitChild is only ever
//     used for that same lookup and nothing else.
//   - Layout is uniform (key, child) pairs (slot 2j/2j+1), chosen so every
//     child — including slot 0 — carries its own explicit min-key (see the
//     internal-node-layout discussion). That means, unlike the leaf-split
//     comment above about promotion vs copy-up: under THIS layout the pivot
//     pair's key is copied up to the parent as the new separator (once the
//     ancestor-update loop below is filled in) but ALSO travels with its
//     child into the new right node, exactly like splitChild's leaf pivot.
//     Nothing is "spent" — the redundancy is the whole point of this layout.
//   - pivot is chosen key-index-first (num_keys/2) then doubled to land on a
//     slot boundary, so it always lands exactly on a (key, child) pair —
//     never splits one down the middle. No tombstone-walk like splitChild's
//     leaf pivot needs: internal key slots are never tombstoned (no
//     internal-node deletion path exists yet — see findRecord()'s comment).
//   - internal nodes don't participate in sibling-linked range scans (only
//     leaves do), so no left/right sibling bookkeeping — SlottedPage::init()
//     already defaults both to INVALID_PAGE_ID.
page_id_t BPlusTree::splitInternal(BTStack bt_stack, Key internal){
    page_id_t node_page_id = bt_stack.back().page_id;
    char* new_page_data = new char[PAGE_SIZE];
    char* node_page_data = new char[PAGE_SIZE];
    page_id_t fresh_page_id = disk_manager_->allocatePage();
    // See the matching comment in splitChild() — fresh_page_id was just
    // allocated and hasn't been written yet, so reading it back would throw
    // "Read past EOF". new_page is fully built via init() below instead.
    disk_manager_->readPage(node_page_id, node_page_data);

    SlottedPage node_page(node_page_data);
    slot_id_t slot_count = node_page.getSlotCount();       // n = 2 * num_keys
    slot_id_t num_keys = static_cast<slot_id_t>(slot_count / 2);
    slot_id_t pivot_key = static_cast<slot_id_t>(num_keys / 2);
    slot_id_t pivot = static_cast<slot_id_t>(pivot_key * 2); // always pair-aligned

    SlottedPage new_page(new_page_data);
    new_page.init(fresh_page_id, SlottedPageType::INTERNAL_PAGE);

    //move [pivot, end] into new right node — same loop shape as splitChild,
    //just without sibling-pointer bookkeeping
    for(slot_id_t slot = pivot; slot < slot_count; ++slot){
        auto [bytes, len] = node_page.getRecord(slot);
        new_page.insertRecord(bytes, len);
        node_page.deleteRecord(slot);
    }
    node_page.compactify();

    // Pivot pair is copied up, not promoted (see the comment above this
    // function) — it also travels with its child into new_page, landing at
    // new_page's own slot 0.
    auto [separator_bytes, separator_len] = new_page.getRecord(0);
    Key separator_key = extractKey(reinterpret_cast<const uint8_t*>(separator_bytes), separator_len);

    // Persist before splitRoot()/insertIntoParent() — splitRoot() re-reads
    // node_page_id (as left_child_id) to derive the left subtree's min-key.
    disk_manager_->writePage(node_page_id, node_page_data);
    disk_manager_->writePage(fresh_page_id, new_page_data);

    BTStack parent_stack = bt_stack;
    parent_stack.pop_back(); // node_page_id's own parent, one level up from node_page_id itself
    if(parent_stack.empty()){
        // node_page_id had no parent — it WAS the root.
        splitRoot(node_page_id, separator_key, fresh_page_id);
    } else {
        insertIntoParent(parent_stack, separator_key, fresh_page_id);
    }

    delete[] new_page_data;
    delete[] node_page_data;
    return fresh_page_id;
}

// Called once the node that just split (leaf or internal) turns out to have
// had no parent — bt_stack was empty, meaning that node WAS root_page_id.
// Builds a fresh two-child internal root over {left_child_id, right_child_id}
// and repoints the tree at it.
//
// left_child_id's own min-key doesn't need a walk down to the leftmost leaf:
// under strict min-key with the explicit-min-key-per-child layout, whatever
// key already sits at left_child_id's own slot 0 (raw key slot if it's an
// internal page, first tuple if it's a leaf) IS the subtree's true minimum,
// by induction over that same invariant one level down. So a single read of
// left_child_id suffices regardless of its page type.
page_id_t BPlusTree::splitRoot(page_id_t left_child_id, Key separator_key, page_id_t right_child_id){
    char* left_page_data = new char[PAGE_SIZE];
    disk_manager_->readPage(left_child_id, left_page_data);
    SlottedPage left_page(left_page_data);
    auto [min_bytes, min_len] = left_page.getRecord(0);
    Key left_min_key = extractKey(reinterpret_cast<const uint8_t*>(min_bytes), min_len);

    page_id_t new_root_id = disk_manager_->allocatePage();
    char* new_root_data = new char[PAGE_SIZE];
    SlottedPage new_root(new_root_data);
    new_root.init(new_root_id, SlottedPageType::INTERNAL_PAGE);

    // Fresh empty page, records inserted in already-ascending order — append
    // is equivalent to a sorted insert here, no Slot[] shifting needed (see
    // SlottedPage's BULK/APPEND-SORTED FAST PATH note).
    new_root.insertRecord(reinterpret_cast<const char*>(left_min_key.GetData()), left_min_key.GetSize());
    new_root.insertRecord(reinterpret_cast<const char*>(&left_child_id), sizeof(left_child_id));
    new_root.insertRecord(reinterpret_cast<const char*>(separator_key.GetData()), separator_key.GetSize());
    new_root.insertRecord(reinterpret_cast<const char*>(&right_child_id), sizeof(right_child_id));

    root_page_id = new_root_id;
    disk_manager_->writePage(new_root_id, new_root_data);
    //root_page_id itself isn't persisted anywhere on disk yet (DiskManager's
    //GlobalMetadata has a root_page_id field but no setter) — a process
    //restart still loses the root pointer. Out of scope here.

    delete[] left_page_data;
    delete[] new_root_data;
    return new_root_id;
}
// Inserts a new (separator_key, new_right_page_id) separator pair into the
// correct ancestor, recursively splitting that ancestor first if it's full.
// Shared by splitChild() and splitInternal() — both arrive here having
// already built their new right sibling and just need it wired into the
// tree above. bt_stack.back().page_id must be the DIRECT parent to insert
// into (same contract splitInternal() itself expects of its own bt_stack).
void BPlusTree::insertIntoParent(BTStack bt_stack, Key separator_key, page_id_t new_right_page_id){
    page_id_t parent_page_id = bt_stack.back().page_id;
    char* parent_page_data = new char[PAGE_SIZE];
    disk_manager_->readPage(parent_page_id, parent_page_data);
    SlottedPage parent_page(parent_page_data);
    uint16_t needed_space = separator_key.GetSize() + sizeof(page_id_t) + sizeof(Slot) * 2; //[key, page_id] slot pair

    if(parent_page.getFreeSpace() < needed_space){
        parent_page.compactify();
        // Internal-node key slots are never tombstoned (no internal-node
        // delete path exists yet), so compactify() never drops an entry
        // here — logical slot positions are unaffected, nothing to reform.
    }
    if(parent_page.getFreeSpace() < needed_space){
        // Still doesn't fit — parent_page itself must split first.
        // bt_stack.back().page_id already IS parent_page_id, which is
        // exactly what splitInternal expects (see its doc comment), so it's
        // passed through unmodified rather than trimmed.
        page_id_t new_candidate_parent = splitInternal(bt_stack, separator_key);

        char* new_parent_page_data = new char[PAGE_SIZE];
        disk_manager_->readPage(new_candidate_parent, new_parent_page_data);
        SlottedPage new_parent_page(new_parent_page_data);

        // First slot should always exist — splitInternal never returns an
        // empty right node. Keys can't tie (would imply a duplicate key),
        // so this comparison always picks a side.
        auto [first_key_bytes, first_key_len] = new_parent_page.getRecord(0);
        Key first_key = extractKey(reinterpret_cast<const uint8_t*>(first_key_bytes), first_key_len);
        if(first_key.Compare(separator_key) < 0){
            // new node's smallest key is still <= separator_key —
            // separator_key belongs in the new right node, not the original.
            parent_page_id = new_candidate_parent;
        }
        delete[] new_parent_page_data;

        // parent_page_id's on-disk contents changed under splitInternal —
        // re-read so parent_page reflects the post-split state rather than
        // the pre-split copy taken above.
        disk_manager_->readPage(parent_page_id, parent_page_data);
        parent_page = SlottedPage(parent_page_data);
    }

    // parent_page/parent_page_id are now the correct direct parent.
    // Binary search for separator_key among its (key, child) slot pairs —
    // same shape as findRecord()'s internal-node search — to find where the
    // new separator pair belongs.
    slot_id_t n = parent_page.getSlotCount();
    slot_id_t num_keys = static_cast<slot_id_t>(n / 2);
    slot_id_t lo = 0, hi = num_keys;
    while(lo < hi){
        slot_id_t mid = static_cast<slot_id_t>(lo + (hi - lo) / 2);
        auto [bytes, len] = parent_page.getRecord(static_cast<slot_id_t>(mid * 2));
        Key key = extractKey(reinterpret_cast<const uint8_t*>(bytes), len);
        if(key.Compare(separator_key) < 0) lo = static_cast<slot_id_t>(mid + 1);
        else hi = mid;
    }
    slot_id_t logical_pos = static_cast<slot_id_t>(lo * 2);
    parent_page.insertRecordAt(logical_pos, reinterpret_cast<const char*>(separator_key.GetData()), separator_key.GetSize());
    parent_page.insertRecordAt(static_cast<slot_id_t>(logical_pos + 1),
                                reinterpret_cast<const char*>(&new_right_page_id), sizeof(new_right_page_id));
    disk_manager_->writePage(parent_page_id, parent_page_data);

    delete[] parent_page_data;
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
