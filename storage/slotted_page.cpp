#include "slotted_page.h"

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

/* INTERNAL DATA: char* data_ pointing to the start of the page (also start of page header) */ 
/*
     * Wraps the PAGE_SIZE buffer pointed to by `data`.
     * Does NOT zero-initialise. Caller must call init() on a fresh page
     * or simply wrap an existing, already-formatted page.
     * `data` must remain valid for the lifetime of this SlottedPage.
     */
SlottedPage::SlottedPage(char* data){
	data_ = data; 
}

    /*
     * Writes the initial PageHeader into data[] with the given page_type.
     * Sets slot_count = 0, free_space_ptr = PAGE_SIZE (heap starts at end).
     * Must be called exactly once on a freshly allocated page before any
     * insertRecord() call. Calling this on an existing page destroys its data.
     */
void SlottedPage::init(page_id_t assigned_id, PageType type) {
	// 1. Create the header with 'zeroed' or 'initial' metadata
	PageHeader header;
	header.page_id = assigned_id;
	header.page_type = type;
	header.max_slot_id = 0;
	header.lsn = 0;        // Initial state
	header.check_sum = 0;  // Will be computed on Disk Write
	
	// 2. Set the pointers for the "Tectonic Plate" design
	// Records grow UP from the end of the page
	header.free_space_ptr = PAGE_SIZE; 
	
	// 3. Write the header struct into the start of your raw data buffer
	// (Assuming data is a char[] or uint8_t[] member of your Page class)
	std::memcpy(data_, &header, sizeof(PageHeader));
	
	// 4. Optional: Zero out the rest of the page to prevent "dirty" data
	std::memset(data_ + sizeof(PageHeader), 0, PAGE_SIZE - sizeof(PageHeader));
}

    /*
     * Copies `length` bytes from `record` into the record heap, growing it
     * backward. Allocates a new slot entry at index slot_count, stores the
     * record's (offset, length), increments slot_count.
     * Returns the new slot_id (== old slot_count before increment).
     *
     * Returns std::nullopt if:
     *   - There is not enough free space even after compactify() — caller
     *     must split the page.
     *   - `length` == 0 (zero-length records are not allowed; use tombstones).
     *
     * Does NOT call compactify() automatically — caller decides when to compact.
     * After a successful insert, caller must mark the Page dirty.
     */
std::optional<slot_id_t> SlottedPage::insertRecord(const char* record, uint16_t length){
	if(length == 0) return std::nullopt; 
	//check there is enough space
	PageHeader* header = GetHeader();
	uint16_t first_free_slot_id = find_first_free_slot_id();
	bool needs_new_slot = (first_free_slot_id == header->max_slot_id);
	uint16_t space_needed = length + (needs_new_slot ? sizeof(Slot) : 0);
	if(space_needed > getTotalFreeSpace()) return std::nullopt; 

	//if need to compact, compact 
	if(!canInsertContigious(length, needs_new_slot)){
		compactify();
	}
	
	//insert record 
	header->free_space_ptr -= length; 
	uint16_t new_offset = header->free_space_ptr; 
	std::memcpy(data_ + new_offset, record, length); 

	//modify slot in-place
	Slot* slot_ptr = GetSlot(first_free_slot_id).value_or(nullptr); //ensure we are guaranteed this is within bounds... perhaps add an assert? 
	assert(slot_ptr != nullptr); //YOU FUCKED UP!
	slot_ptr->offset = new_offset; 
	slot_ptr->size = length; 

	if(needs_new_slot) header->max_slot_id++;
	return first_free_slot_id; 
}

    /*
     * Marks slot `slot_id` as deleted by setting Slot.length = 0 (tombstone).
     * The bytes in the heap are not zeroed. Free space does not increase until
     * compactify() reclaims the gap.
     * Returns false if slot_id is out of range or already deleted.
     * After a successful delete, caller must mark the Page dirty.
     */
 bool SlottedPage::deleteRecord(slot_id_t slot_id){
	PageHeader* header = GetHeader(); 
	//assumes slot_id is always its index 
	if(header->slot_count - 1 > slot_id){
		cout << "Slot id out of range" << '\n';
		return false; 
	}
	//HAVE TO MODIFY
	broken_below();
	uint16_t slot_offset = sizeof(PageHeader) + sizeof(Slot) * slot_id; 
	Slot* slot = reinterpret_cast<Slot*>(data_ + slot_offset); 
	slot->offset = 0;
	std::memcpy(data_ + slot_offset, slot, sizeof(Slot)); 
	return true; 
 }

    /*
     * Returns a span (pointer + length) into data[] for the record at `slot_id`.
     * The span is valid until the next insertRecord() or compactify() call —
     * both can shift record positions. Callers that need the data beyond that
     * must copy it out.
     * Returns an empty span if slot_id is out of range or deleted.
     */
/*
 * two options: if GetSlot() returns nullopt, propagate, OR just return an empty span. I'm lazy so I go with empty span, but 
 */
std::span<const char> SlottedPage::getRecord(slot_id_t slot_id) const{
	auto slot = GetSlot(slot_id).value_or(nullptr); 
	if(!slot) return {}; //empty span 
	return { data_ + slot->offset, slot->size }; 
}

    /*
     * Overwrites the record at `slot_id` with `length` bytes from `record`.
     * Only valid if new length == old length (same-size update, no movement).
     * For different-size updates, the caller must deleteRecord + insertRecord.
     * Returns false if slot_id is invalid, deleted, or lengths differ.
     * After success, caller must mark the Page dirty.
     */
bool SlottedPage::updateRecord(slot_id_t slot_id, const char* record, uint16_t length){
	Slot* slot = GetSlot(slot_id).value_or(nullptr);
	if(!slot) return false; 
	//slot sizes must match (im lazy) 
	if(slot->size != length) return false; 
	memcpy(data_ + slot->offset, record, length); 
	return true; 
}	

    /*
     * Defragments the record heap in-place. Scans all live slots, packs their
     * records contiguously at the end of the page, updates slot offsets to
     * match new positions, resets free_space_ptr. Slot indices (slot_ids)
     * do NOT change — this is the invariant the B+Tree depends on.
     * Should be called when getFreeSpace() < needed but getTotalFreeSpace()
     * (including fragmented gaps) >= needed.
     * After calling this, any previously obtained getRecord() spans are stale.
     */
void SlottedPage::compactify(){
	//sort by offset descending
	PageHeader* header = GetHeader();
	std::vector<Slot*> live_slots;
	for(uint16_t i = 0;i < header->max_slot_id;i++){
		Slot* slot = GetSlot(i).value_or(nullptr);  
		if(slot && slot->offset != 0){
			live_slots.push_back(slot);
		}
	}
	std::sort(live_slots.begin(), live_slots.end(), 
		[](Slot* a, Slot* b){
			return a->offset > b->offset; 	
		}
	);

	//compactify records 
	uint16_t current_free_ptr = PAGE_SIZE;
	for(Slot* slot : live_slots){
		current_free_ptr -= slot->size; 
		if(slot->offset != current_free_ptr){
			memmove(data_ + current_free_ptr, data_ + slot->offset, slot->size);
			slot->offset = current_free_ptr; 
		}
	}
	header->free_space_ptr = current_free_ptr; 
	
	//slot array trimming - we cant directly compactify slots because that would require changing all the references in our B+ Tree which would be inordinately expensive.
	uint16_t new_max_slot_id = header->max_slot_id; 
	for(int i = header->max_slot_id-1;i >= 0;i--){
		Slot* slot = GetSlot(i).value_or(nullptr); 
		if(slot && slot->offset != 0){
			new_max_slot_id = i+1; 
			break;
		}
		//all slots are dead.
		if(i == 0) new_max_slot_id == 0;
	}
	header->max_slot_id = new_max_slot_id; 
}

    /*
     * Returns the number of bytes in the contiguous free gap between the
     * end of the slot array and free_space_ptr. This is what is immediately
     * available for a new insert WITHOUT compaction.
     * Equation: free_space_ptr - (sizeof(Header) + slot_count * sizeof(Slot))
     */
uint16_t SlottedPage::getFreeSpace() const {
	const PageHeader* header = GetHeader();
	int32_t space = header->free_space_ptr - (sizeof(PageHeader) + sizeof(Slot) * header->max_slot_id); 
	return space;
}

    /*
     * Returns total reclaimable free bytes: contiguous free space plus the
     * sum of lengths of all deleted (tombstoned) slots. If this is >= the
     * needed size but getFreeSpace() is not, compactify() will help.
     */
uint16_t SlottedPage::getTotalFreeSpace() const {
	const PageHeader* header = GetHeader();
	//start with gap in the middle 
	int32_t space = header->free_space_ptr - (sizeof(PageHeader) + sizeof(Slot) * header->max_slot_id); 
	//add back space from dead slots - tombstones 
	for(uint16_t i = 0;i < header->max_slot_id;i++){
		auto slot = GetSlot(i);
		if(!slot && (*slot)->offset == 0){
			space += (*slot)->size;
		}
	}
	return (space < 0) ? 0 : static_cast<uint16_t>(space);
};

bool SlottedPage::canInsertContigious(uint16_t length, bool needs_new_slot) const {
	uint16_t slot_growth = needs_new_slot ? sizeof(Slot) : 0;
	uint16_t current_gap = getFreeSpace(); 
	return current_gap >= (length + slot_growth);
}

    /*
     * Returns the number of slot entries (live + deleted). The last valid
     * slot_id is getSlotCount() - 1. Does NOT count only live records.
     */
    // uint16_t getSlotCount() const;

    /*
     * Returns the PageType stored in the header. Used by upper layers
     * to distinguish leaf pages, internal pages, overflow pages, etc.
     */
    // PageType getPageType() const;

    /*
     * Returns a mutable pointer to the header at offset 0.
     * You will define PageHeader as a packed struct in the .cpp or a
     * separate header. Its size is fixed at compile time and must be
     * documented in config.h (SLOTTED_PAGE_HEADER_SIZE).
     */
    // PageHeader* header();
    // const PageHeader* header() const;

    /*
     * Returns a pointer to the slot array entry at index `slot_id`.
     * Slot array starts immediately after the header.
     * Does no bounds checking — use getSlotCount() before calling.
     */
    // Slot* slotAt(slot_id_t slot_id);
    // const Slot* slotAt(slot_id_t slot_id) const;
