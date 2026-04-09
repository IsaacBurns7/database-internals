#pragma once
#include "common/types.h"
#include "common/config.h"
#include <cstdint>
#include <optional>
#include <span>  // C++20; use std::pair<char*,uint16_t> if on C++17
#include <cstring>

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
enum PageType: uint8_t{ 
	master,
	internal,
	leaf,
	freelist, //tracks empty pages 
	overflow //for like 10KB strings  
};

struct alignas(4) PageHeader {
    page_id_t page_id;      // 4 bytes - Offset 0
    lsn_t lsn;              // 4 bytes - Offset 4
    uint32_t check_sum;     // 4 bytes - Offset 8
    uint16_t slot_count;    // 2 bytes - Offset 12
    uint16_t free_space_ptr;// 2 bytes - Offset 14, first free space for the next record 
    PageType page_type;     // 1 byte  - Offset 16
    uint8_t padding[3];     // 3 bytes - Explicitly pad to 20 bytes (4-byte alignment)
};

//need size for variable-length records (strings!!!)
struct Slot{
	uint16_t offset;
	uint16_t size; 
};

class SlottedPage {
public:
    /*
     * Wraps the PAGE_SIZE buffer pointed to by `data`.
     * Does NOT zero-initialise. Caller must call init() on a fresh page
     * or simply wrap an existing, already-formatted page.
     * `data` must remain valid for the lifetime of this SlottedPage.
     */
    explicit SlottedPage(char* data);

    /*
     * Writes the initial PageHeader into data[] with the given page_type.
     * Sets slot_count = 0, free_space_ptr = PAGE_SIZE (heap starts at end).
     * Must be called exactly once on a freshly allocated page before any
     * insertRecord() call. Calling this on an existing page destroys its data.
     */
    void init(page_id_t page_id, PageType page_type);

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
    std::optional<slot_id_t> insertRecord(const char* record, uint16_t length);

    /*
     * Marks slot `slot_id` as deleted by setting Slot.length = 0 (tombstone).
     * The bytes in the heap are not zeroed. Free space does not increase until
     * compactify() reclaims the gap.
     * Returns false if slot_id is out of range or already deleted.
     * After a successful delete, caller must mark the Page dirty.
     */
    bool deleteRecord(slot_id_t slot_id);

    /*
     * Returns a span (pointer + length) into data[] for the record at `slot_id`.
     * The span is valid until the next insertRecord() or compactify() call —
     * both can shift record positions. Callers that need the data beyond that
     * must copy it out.
     * Returns an empty span if slot_id is out of range or deleted.
     */
    std::span<const char> getRecord(slot_id_t slot_id) const;

    /*
     * Overwrites the record at `slot_id` with `length` bytes from `record`.
     * Only valid if new length == old length (same-size update, no movement).
     * For different-size updates, the caller must deleteRecord + insertRecord.
     * Returns false if slot_id is invalid, deleted, or lengths differ.
     * After success, caller must mark the Page dirty.
     */
    bool updateRecord(slot_id_t slot_id, const char* record, uint16_t length);

    /*
     * Defragments the record heap in-place. Scans all live slots, packs their
     * records contiguously at the end of the page, updates slot offsets to
     * match new positions, resets free_space_ptr. Slot indices (slot_ids)
     * do NOT change — this is the invariant the B+Tree depends on.
     * Should be called when getFreeSpace() < needed but getTotalFreeSpace()
     * (including fragmented gaps) >= needed.
     * After calling this, any previously obtained getRecord() spans are stale.
     */
    void compactify();

    /*
     * Returns the number of bytes in the contiguous free gap between the
     * end of the slot array and free_space_ptr. This is what is immediately
     * available for a new insert WITHOUT compaction.
     * Equation: free_space_ptr - (sizeof(Header) + slot_count * sizeof(Slot))
     */
    uint16_t getFreeSpace() const;

    /*
     * Returns total reclaimable free bytes: contiguous free space plus the
     * sum of lengths of all deleted (tombstoned) slots. If this is >= the
     * needed size but getFreeSpace() is not, compactify() will help.
     */
    uint16_t getTotalFreeSpace() const;

    /*
     * Returns the number of slot entries (live + deleted). The last valid
     * slot_id is getSlotCount() - 1. Does NOT count only live records.
     */
    uint16_t getSlotCount() const;

    /*
     * Returns the PageType stored in the header. Used by upper layers
     * to distinguish leaf pages, internal pages, overflow pages, etc.
     */
    PageType getPageType() const;

private:
    char* data_;  // points into Page::data_[] — not owned here
	
	// Internal helper to get a pointer to the header bytes
    inline PageHeader* GetHeader() {
        return reinterpret_cast<PageHeader*>(data_);
    }
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
};
