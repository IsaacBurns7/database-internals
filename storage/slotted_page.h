#pragma once
#include "common/types.h"
#include "common/config.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <cstring>
#include <iostream>
#include <cassert>
#include <vector>
#include <algorithm>

using std::cout;

/*
 * SlottedPage — interprets a raw PAGE_SIZE byte buffer as a slotted page.
 *
 * Responsibilities:
 *   - Imposes the physical layout: header | slot array → ... ← record heap.
 *     The slot array grows from the start of the data area forward;
 *     records pack from the end of the page backward. Free space is the
 *     gap between them. This layout supports variable-length records without
 *     fragmentation.
 *   - Exposes stable slot_id handles. A slot_id never changes after insert,
 *     even if compactify() moves the underlying bytes. This is the contract
 *     that B+Tree leaf nodes depend on when storing (key → slot_id) pairs.
 *   - Performs compaction (in-place defragmentation) when free space exists
 *     in total but is fragmented (deleted gaps in the record heap).
 *
 * What SlottedPage deliberately does NOT do:
 *   - It does not own the buffer. It wraps a pointer to Page::data_[].
 *     The Page (and thus BufferPoolManager) owns the memory.
 *   - It does not perform I/O or log modifications.
 *   - It does not interpret record contents — records are opaque byte spans.
 *
 * Physical layout of Page::data_[PAGE_SIZE]:
 *
 *   ┌──────────────────────────────────────────────────────────┐
 *   │  PageHeader (fixed size, at offset 0)                    │
 *   │    page_type, slot_count, free_space_ptr, lsn, ...       │
 *   ├──────────────────────────────────────────────────────────┤
 *   │  Slot[0]  { offset: uint16, length: uint16 }             │
 *   │  Slot[1]  ...                                            │
 *   │  Slot[N-1]                         ↓ grows forward       │
 *   ├────────────────────────  free space  ────────────────────┤
 *   │                                    ↑ grows backward      │
 *   │  record for slot K  (bytes, variable length)             │
 *   │  record for slot 1                                       │
 *   │  record for slot 0                                       │
 *   └──────────────────────────────────────────────────────────┘
 *
 * Deleted slots: set Slot.length = 0 (tombstone). The bytes remain in the
 * heap until compactify() is called. slot_count does NOT decrease — slot
 * indices are never reused within a page lifetime.
 *
 * Design decisions to make before implementing:
 *   - PageHeader layout: how many bytes? must fit page_type, slot_count,
 *     free_space_ptr, reserved space for future fields. Keep it a multiple
 *     of 8 bytes for alignment. This size is FIXED FOREVER once you write
 *     your first page to disk.
 *   - Slot width: 4 bytes (uint16 offset + uint16 length) is typical.
 *     If you want records > 64KB, use uint32 — but that halves max slots.
 *   - Overflow pages: records larger than PAGE_SIZE/2 need a separate
 *     overflow chain. Defer this until B+Tree forces the issue.
 */
enum SlottedPageType: uint8_t{ 
	// master, //custom impl 
	INTERNAL_PAGE,
	LEAF_PAGE,
	// freelist, //tracks empty pages 
	// overflow //for like 10KB strings  
};

struct alignas(8) SlottedPageHeader {
    page_id_t page_id;      // 4 bytes - Offset 0
    lsn_t lsn;              // 4 bytes - Offset 4
    uint32_t check_sum;     // 4 bytes - Offset 8
    uint16_t max_slot_id;    // 2 bytes - Offset 12
	uint16_t free_space_ptr;// 2 bytes - Offset 14, first free space for the next record 
    SlottedPageType page_type;     // 1 byte  - Offset 16
    page_id_t left_sibling; //4 bytes - offset 17 
    page_id_t right_sibling; //4 bytes - offset 21
    char padding[3]; 
    //does this fill in padding? 
};

//need size for variable-length records (strings!!!)
struct Slot{
	uint16_t offset;
	uint16_t size; 
};

class SlottedPage {
public:
    explicit SlottedPage(char* data);
    void init(page_id_t page_id, SlottedPageType page_type);
    std::optional<slot_id_t> insertRecord(const char* record, uint16_t length);
    std::optional<slot_id_t> insertRecordAt(slot_id_t logical_pos, const char* record, uint16_t length);
    bool deleteRecord(slot_id_t slot_id);
    std::pair<const char*, uint16_t> getRecord(slot_id_t slot_id) const;
    bool updateRecord(slot_id_t slot_id, const char* record, uint16_t length);
    void compactify();
    uint16_t getFreeSpace() const;
    uint16_t getTotalFreeSpace() const;
    uint16_t getSlotCount() const;
    SlottedPageType getPageType() const;
    page_id_t getRightSibling() const; 
    page_id_t getLeftSibling() const;
    void setLeftSibling(page_id_t left_sibling);
    void setRightSibling(page_id_t right_sibling);
private:
    char* data_;  // points into Page::data_[] — not owned here
	
	// Internal helper to get a pointer to the header bytes
    inline const SlottedPageHeader* GetHeader() const {
    	return reinterpret_cast<SlottedPageHeader*>(data_);
    }
	inline SlottedPageHeader* GetHeader() {
        return reinterpret_cast<SlottedPageHeader*>(data_);
    }
	//THE BELOW POINTER HAS TO BE CONST BECAUSE HOW ELSE TO GUARANTEE THAT PAGE IS NOT BEING MODIFIED?
	inline std::optional<const Slot*> GetSlot(slot_id_t slot_id) const {
		const SlottedPageHeader* header = GetHeader(); 
		if(slot_id >= header->max_slot_id) return std::nullopt; 
		uint16_t slot_offset = sizeof(SlottedPageHeader) + sizeof(Slot) * slot_id; 
		Slot* slot = reinterpret_cast<Slot*>(data_ + slot_offset);
		cout << "Fetched slot as: " << slot << " with attributes... (offset=" << slot->offset << ",size=" << slot->size << '\n';
		return slot; 
	}
	inline std::optional<Slot*> GetSlot(slot_id_t slot_id) {
		SlottedPageHeader* header = GetHeader(); 
		if(slot_id >= header->max_slot_id) return std::nullopt; 
		uint16_t slot_offset = sizeof(SlottedPageHeader) + sizeof(Slot) * slot_id; 
		Slot* slot = reinterpret_cast<Slot*>(data_ + slot_offset);
		return slot; 
	}
	bool canInsertContigious(uint16_t length, bool needs_new_slot) const; 
	uint16_t find_first_free_slot_id() const{
		const SlottedPageHeader* header = GetHeader(); 
		for(uint16_t i = 0;i < header->max_slot_id; i++){
			const Slot* slot = GetSlot(i).value_or(nullptr); 
			assert(slot != nullptr); 
			if(slot->offset == 0) return i; 
		}
		return header->max_slot_id;
	}
};

/*
 * ============================================================================
 * REFACTORING DIRECTIONS — candidate designs for fixing the insertion-order
 * problem (insertRecord() currently appends at find_first_free_slot_id(),
 * i.e. insertion order, not key order — see the FIX_SLOT_ID discussion).
 * None of these are implemented. Each entry states: the core change, the
 * invariant it establishes (what becomes true/false about slot identity and
 * order after adopting it), and why that invariant is useful to have.
 * ============================================================================
 *
 * (1) IN-PLACE SORTED SHIFT — insertRecordAt(logical_pos, record, len)
 *     Change: caller (BPlusTree) computes the sorted position; SlottedPage
 *       memmove's the Slot[] array to open a gap at that index, writes the
 *       new slot there, max_slot_id++. Same idea applies to delete: shift
 *       the array down instead of tombstoning.
 *     Invariant: physical slot order == key order, always. Slot rank is
 *       NOT stable — inserting/deleting anywhere before an entry changes
 *       every later entry's index. No external structure may hold onto a
 *       slot index across a mutation of the same page.
 *     Why useful: this is the minimum change that makes findRecord's binary
 *       search valid and BPlusTreeIterator::Next()'s ascending-walk
 *       assumption true. Cheapest to implement, cheapest to reason about,
 *       and matches how Postgres's nbtree (B-tree index) pages behave —
 *       see FIX_SLOT_ID appendix in analysis.md for the full comparison.
 *
 * (2) LOGICAL-ORDER INDIRECTION ARRAY (the design sketched in the original
 *     findRecord() comments, kept here for contrast)
 *     Change: physical Slot[] stays append-only/stable forever (today's
 *       behavior, untouched). A second small array of slot indices, kept
 *       sorted by key, is maintained alongside it; binary search walks the
 *       order array, not the physical array. Insert/delete only ever
 *       memmove entries within the (tiny, index-sized) order array.
 *     Invariant: physical slot_id is a permanent handle — once assigned, it
 *       never changes for the lifetime of the page. Order is enforced by a
 *       second structure, not by physical position.
 *     Why useful: matters only if something outside the page will someday
 *       store (page_id, slot_id) as a durable pointer — e.g. a future
 *       secondary index pointing into these pages the way Postgres TIDs
 *       point into heap pages. Not needed for this project today (no
 *       secondary indexes exist), so this is strictly more bookkeeping
 *       (an extra array, an extra indirection on every lookup) for an
 *       invariant nothing currently relies on. Worth revisiting only if
 *       secondary indexes get planned.
 *
 * (3) REDIRECT-ON-DEMAND (hybrid of 1 and 2, modeled on Postgres HOT /
 *     LP_REDIRECT line pointers)
 *     Change: default to (1) — slots shift freely. But the moment some
 *       external structure needs a durable handle to a specific record, that
 *       slot is converted to a permanent "redirect" stub (never moves again,
 *       never reused) whose payload is just "the record actually lives at
 *       slot X now" — updated whenever the real entry's position changes.
 *     Invariant: slot stability is opt-in and paid for only by the records
 *       that need it, not by every record in the page.
 *     Why useful: gets you (2)'s external-pointer durability without (2)'s
 *       blanket overhead. Meaningfully more complex to implement/test than
 *       either (1) or (2) alone — only worth it if most records never need a
 *       durable external handle but a few do.
 *
 * (4) SPLIT INTO TWO PAGE CONTRACTS — HeapPage vs IndexPage
 *     Change: factor today's SlottedPage into a shared low-level "physical
 *       slot array + record heap" primitive, then layer two distinct
 *       policies on top: a Heap policy (append/reuse-first-free, slot_id
 *       stable forever, no order guarantee — today's actual behavior) and an
 *       Index policy (sorted insert/delete via (1), rank not stable). Pick
 *       the policy per page based on SlottedPageType.
 *     Invariant: each page type gets an honest, distinct contract instead of
 *       one class trying to satisfy both at once.
 *     Why useful: this codebase already has heap_organized_table.cpp and
 *       index_organized_table.cpp under ch1/, i.e. heap-organized tables are
 *       an explicit future direction, not a hypothetical. The current bug is
 *       exactly "a heap-page policy (find_first_free_slot_id) applied to a
 *       page that's actually playing an index-page role" — splitting the
 *       contract now means this exact confusion can't recur when a real heap
 *       table gets built later.
 *
 * (5) BULK/APPEND-SORTED FAST PATH for splitChild()
 *     Change: add an insertSortedRange()-style bulk op for the specific case
 *       of populating a freshly-initialized (empty) page with records already
 *       known to be in ascending key order — e.g. the right half of a split.
 *       On an empty page, "append in order" and "insert in sorted position"
 *       are the same operation, so no per-record shifting is needed at all.
 *     Invariant: none new — this is a fast path for a case (1) already
 *       handles correctly, not a new ordering guarantee.
 *     Why useful: splitChild() today does per-record delete+insert in a loop;
 *       once (1) makes every insert a potential O(n) shift, splitting a full
 *       page one record at a time becomes O(n^2). A bulk path restores O(n)
 *       for the one place this actually matters — worth flagging even though
 *       it's a performance refactor, not a correctness one.
 */
