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
