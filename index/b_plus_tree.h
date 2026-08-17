#ifndef INDEX_TREE

#define INDEX_TREE

#include "storage/disk_manager.h"
#include "storage/slotted_page.h"
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

// PENDING (VPID/PPID directory, see storage/disk_manager.h design notes):
// Breadcrumb::page_id does NOT need to become (page_id, generation). It's
// only ever held from findRecord() down through the same synchronous
// call that consumes bt_stack (insert/remove/splitChild/insertIntoParent) —
// never cached across a gap where something else could deallocate the page
// out from under it. Contrast with BPlusTreeIterator::page_id_ below, which
// IS held across caller-controlled gaps (separate Next() calls) and is the
// one place in this file the generation-check machinery would actually apply.
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
public:
	BPlusTree(DiskManager* disk_manager, page_id_t root_page_id, const Schema* schema,
	          uint32_t key_col_idx, TypeId key_type_id = TypeId::NUMERIC, uint8_t key_width = 8)
	    : disk_manager_(disk_manager), root_page_id(root_page_id), key_type_id_(key_type_id),
	      key_width_(key_width), schema_(schema), key_col_idx_(key_col_idx) {}

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
	// TEST_F generates a fresh subclass per test case, and friendship isn't
	// inherited — a single dedicated accessor struct (defined in
	// tests/index/b_plus_tree.cpp) lets every test reach extractKey/findRecord
	// through one friend declaration instead of one FRIEND_TEST per test case.
	friend struct BPlusTreeTestAccess;

	Key extractKey(const uint8_t *record, uint16_t len) const;
    FindRecordMetadata findRecord(Key target);
        //use in insert(): needs BTStack
        //use in remove(): needs slot_id_x

	page_id_t splitChild(BTStack bt_stack, Key child); //returns new child
    page_id_t splitInternal(BTStack bt_stack, Key internal); //returns new internal
    void insertIntoParent(BTStack bt_stack, Key separator_key, page_id_t new_right_page_id);
        //shared tail of splitChild/splitInternal: wires (separator_key, new_right_page_id)
        //into bt_stack.back()'s page, recursively splitting it first if needed
    page_id_t splitRoot(page_id_t left_child_id, Key separator_key, page_id_t right_child_id);
        //called when bt_stack was empty — left_child_id (leaf or internal) WAS root_page_id.
        //builds a fresh 2-child internal root and repoints root_page_id at it
	void merge(page_id_t parent_node, BTStack bt_stack, Key left_child); //could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key)
	void redistribute(page_id_t parent_node, BTStack bt_stack, Key child);
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
 	DiskManager* disk_manager_;
	// PENDING (VPID/PPID directory): stays a bare page_id_t (VPID once the
	// directory lands) — same reasoning as Breadcrumb above. root_page_id is
	// re-read fresh at the top of every findRecord() walk, never trusted
	// across a gap, so no generation tag needed here either.
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

/*
 * ============================================================================
 * ITERATOR DESIGN — Next() and the slot_id-shifts-under-mutation problem
 * ============================================================================
 * SlottedPage::insertRecordAt() (analysis.md Appendix 1) makes physical slot
 * order == key order WITHIN a page, but slot_id itself is not a stable
 * handle across mutations: an insert anywhere before a cached slot_id shifts
 * it forward, and compactify() (which now also drops tombstoned entries from
 * Slot[], see slotted_page.cpp) can shift it backward or invalidate it
 * outright. Next() below no longer caches slot_id_ across calls — see
 * DIRECTION (A), implemented below. (B) and (C) are documented but not
 * implemented — both depend on infrastructure that doesn't exist yet.
 *
 * (A) RE-SEEK BY KEY — IMPLEMENTED
 *     Next() caches last_key_ (the last key emitted) instead of a slot_id.
 *     Every call re-runs a binary search (seekFirstAfter(), the same
 *     tombstone-aware shape as BPlusTree::findRecord()'s leaf search) against
 *     the CURRENT state of the page to relocate the next record, rather than
 *     trusting a stored index. The only slot_id ever held onto is
 *     start_slot_, and only until the first call sets last_key_ — after
 *     that, no slot_id is cached again. Costs an extra O(log n) search per
 *     Next() instead of an O(1) increment, in exchange for never trusting a
 *     position across a mutation boundary.
 *
 * (B) LATCH THE PAGE FOR THE ITERATOR'S LIFETIME ON IT — FUTURE, needs latch
 *     crabbing. Once real page-level latching exists (see the "will
 *     implement latch crabbing for concurrency" note on BPlusTree), Next()
 *     could take a read latch on page_id_ on arrival and hold it until
 *     crossing to the right sibling — at which point slot_id_++ becomes
 *     valid again and (A)'s per-call search cost goes away, because
 *     concurrent mutation of a latched page is externally forbidden rather
 *     than defended against by re-deriving position. Not worth building
 *     before latch crabbing lands — there is nothing yet to latch against,
 *     and there is no mutex anywhere in this codebase today (per
 *     analysis.md).
 *
 * (C) MATERIALIZE THE PAGE ONCE PER CROSSING — FUTURE, needs MVCC. Once
 *     multi-version reads exist, the iterator could copy every version-
 *     visible live slot's (key, record) into a local buffer once per sibling
 *     crossing and walk that local copy instead of re-deriving position from
 *     the live page on every Next() call. This is the natural home for
 *     snapshot isolation: "what this iterator sees" would already need to be
 *     pinned to a read timestamp/snapshot, so materializing at that
 *     snapshot once per page is a natural side effect of MVCC rather than an
 *     ad hoc, undocumented staleness window the way it would be without
 *     version tracking. Revisit once MVCC scoping starts.
 * ============================================================================
 */
class BPlusTreeIterator {
public:
    BPlusTreeIterator(BPlusTree *tree, page_id_t start_page, slot_id_t start_slot, Key end):
        tree_(tree), page_id_(start_page), start_slot_(start_slot), end_(end){
            data_ = new char[PAGE_SIZE];
            tree_->disk_manager_->readPage(start_page, data_);
        }
    ~BPlusTreeIterator(){ delete[] data_; }

    // Returns the record at the cursor's current position and advances past
    // it, crossing into the right-sibling leaf when the current page is
    // exhausted. Returns std::nullopt once the extracted key exceeds end_,
    // or once there is no right sibling left to cross into.
    //
    // Implements DIRECTION (A) above: position is re-derived from last_key_
    // via seekFirstAfter() every call, never trusted as a cached slot_id.
    std::optional<std::pair<uint8_t*, uint16_t>> Next(){
        SlottedPage current_page = SlottedPage(data_);
        slot_id_t pos = last_key_.has_value()
            ? seekFirstAfter(current_page, *last_key_)
            : start_slot_;

        while(true){
            slot_id_t n = current_page.getSlotCount();
            while(pos < n && current_page.getRecord(pos).second == 0) pos++; //skip tombstones

            if(pos < n){
                auto [bytes, len] = current_page.getRecord(pos);
                Key key = tree_->extractKey(reinterpret_cast<const uint8_t*>(bytes), len);
                if(key.Compare(end_) > 0) return std::nullopt; //past the range
                last_key_ = key;
                return std::pair<uint8_t*, uint16_t>{(uint8_t*)bytes, len};
            }

            //page exhausted — cross to right sibling and resume from its start
            page_id_t right = current_page.getRightSibling();
            // NOTE: `right` is a page_id read out of an on-disk sibling pointer and
            // dereferenced below with no check that it's still the same logical page —
            // e.g. a remove()-triggered merge running while this iterator is alive
            // could deallocate `right` and have it recycled for something else before
            // we get here. This is the concrete stale-physical-page-id case behind the
            // VPID/PPID directory design in disk_manager.h. Once DiskManager exposes
            // getGeneration()/isStillValid(), this iterator should capture a generation
            // per page (see page_id_ below) and validate `right` against it here rather
            // than trusting it blindly. Not yet implemented — today's single-threaded,
            // non-interleaved usage happens to avoid triggering it.
            if(right == INVALID_PAGE_ID) return std::nullopt;
            page_id_ = right;
            tree_->disk_manager_->readPage(page_id_, data_);
            current_page = SlottedPage(data_);
            pos = 0;
        }
    }

private:
    // Tombstone-aware strict upper_bound: first live slot whose key is
    // greater than `key`. Mirrors BPlusTree::findRecord()'s leaf-level
    // binary search, just with a strict-greater comparator instead of >=,
    // since we want the record AFTER the last one already emitted.
    slot_id_t seekFirstAfter(SlottedPage& page, const Key& key) const {
        slot_id_t lo = 0, hi = page.getSlotCount();
        while(lo < hi){
            slot_id_t mid = static_cast<slot_id_t>(lo + (hi - lo) / 2);
            slot_id_t probe = mid;
            auto rec = page.getRecord(probe);
            while(rec.second == 0 && probe < hi){
                probe = static_cast<slot_id_t>(probe + 1);
                rec = page.getRecord(probe);
            }
            if(probe >= hi){ hi = mid; continue; } //[mid, hi) entirely dead — answer lies left
            Key probe_key = tree_->extractKey(reinterpret_cast<const uint8_t*>(rec.first), rec.second);
            if(probe_key.Compare(key) <= 0) lo = static_cast<slot_id_t>(probe + 1);
            else hi = probe;
        }
        return lo;
    }

	BPlusTree *tree_;
	page_id_t page_id_;              // cached across Next() calls — see stale-reference note above
	slot_id_t start_slot_;          // only consulted before last_key_ is set (the very first call)
	std::optional<Key> last_key_;   // last key emitted; drives seekFirstAfter() on every later call
    Key end_;

    char *data_; //invariant is this is valid
        //should this be a Page?
};

#endif
