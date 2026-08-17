#pragma once
#include "common/types.h"
#include "common/config.h"
#include "storage/freelist_page.h"
#include <string>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <fcntl.h> //open()
#include <unistd.h> //close()
#include <sys/stat.h> //mode constants
#include <system_error>
#include <string.h>

namespace fs = std::filesystem;
using std::cout; 
using std::cerr;
using std::endl;
using std::ofstream;

/*
 * DiskManager — raw I/O abstraction over a single database file.
 *
 * Responsibilities:
 *   - Translates logical page_id values into byte offsets:
 *       offset = page_id * PAGE_SIZE
 *     and issues pread/pwrite syscalls of exactly PAGE_SIZE bytes.
 *   - Owns the free-page list. allocatePage() prefers reusing a deallocated
 *     page over extending the file; otherwise it bumps next_page_id_ and
 *     truncates/extends the file.
 *   - Serialises concurrent I/O if you add threads later (single fd_ today,
 *     add a mutex when needed).
 *
 * What DiskManager deliberately does NOT do:
 *   - It does not cache anything. Every call is a real syscall.
 *     BufferPoolManager is the cache; DiskManager is the backing store.
 *   - It does not know what is inside a page (no header parsing, no slot logic).
 *   - It does not manage transactions or logs.
 *
 * File layout on disk:
 *   [DB header page — page_id 0]   (optional: stores free-list metadata)
 *   [page 1][page 2]...[page N]
 *   Each page is exactly PAGE_SIZE bytes, no gaps, no padding.
 *
 * Design decision — free page tracking:
 *   free_pages_ is kept in memory only. On a clean shutdown you can persist it
 *   into page 0. On a crash you lose it and leak disk pages; the WAL recovery
 *   phase can reconstruct it, or you accept the leak for now. Decide before
 *   implementing page 0.
 */


struct alignas(PAGE_SIZE) GlobalMetadata {
	//maybe add like a "dirty" flag ?? b/c this is recently updated but its gotta be pushed to disk eventually >< 
	//and updating it constantly is kind of expensive >< >< 
	uint32_t magic_number; 
	uint32_t db_version; 
	page_id_t root_page_id; 
	page_id_t freelist_head; //head of freelist chain
	page_id_t next_page_id; //The "High Water Mark" 
	
	static constexpr size_t FIXED_SIZE = sizeof(uint32_t) * 2 + sizeof(page_id_t) * 3; 

	uint8_t unused_padding[PAGE_SIZE - FIXED_SIZE];
};
static_assert(sizeof(GlobalMetadata) == PAGE_SIZE, "GlobalMetadata is not exactly PAGE_SIZE!");


class DiskManager {
public:
    /*
     * Opens (or creates) the database file at file_path.
     * On creation, initialises the file with a header page at offset 0.
     * On open, reads next_page_id_ from the header so allocation resumes
     * where it left off. Throws std::runtime_error if the file cannot be
     * opened or if the header is corrupt.
     *
     * PENDING (VPID/PPID directory): PageDirectory is NOT persisted to disk —
     * it's rebuilt here, in-memory, on every open, by scanning the file:
     *   1. Walk the on-disk freelist chain (global_metadata_.freelist_head ->
     *      Freelist_Page chain) to collect the set of currently-free PPIDs.
     *      This is the existing, already-persisted mechanism — no new format.
     *   2. For every PPID in [1, next_page_id) NOT in that free set, it's a
     *      live page: pread just sizeof(SlottedPageHeader) bytes at its
     *      offset (not the full 4KB), pull (page_id, generation) out of the
     *      header (see slotted_page.h — page_id there becomes the VPID this
     *      page is bound to), and insert directory_[vpid] = {ppid, generation}.
     * No directory pages ever touch disk. Cost is one sequential header-only
     * scan of live pages per process start (boot-time rebuild, not unlike
     * fsck) — acceptable since it happens once, not per-transaction.
     * Deliberately different from the free_pages_ in-memory-only tradeoff
     * described below: losing free_pages_ on crash just leaks disk pages
     * (low stakes). Losing the directory without this rebuild path would
     * make root_page_id (a VPID) point at nothing — total tree loss, not a
     * leak — which is why this needs the self-describing-header mechanism
     * rather than being casually "in-memory only, and that's fine."
     */
    explicit DiskManager(const std::string& file_path);

    /*
     * Closes the file descriptor. Does NOT flush any caller-held buffers —
     * BufferPoolManager must flush all dirty pages before destroying this.
     */
    ~DiskManager();

    /*
     * Writes exactly PAGE_SIZE bytes from `data` to the file at:
     *   offset = page_id * PAGE_SIZE
     * Uses pwrite() so position is not shared with concurrent readers.
     * Caller must ensure `data` points to at least PAGE_SIZE bytes.
     * Throws on I/O error. Does NOT call fsync — durability is LogManager's job.
     *
     * PENDING (VPID/PPID directory, see design notes at bottom of this file):
     * once PageDirectory exists, `page_id` here becomes a VPID and this
     * function translates it to a PPID via directory_.Translate() before
     * computing the offset. Signature/caller contract is unchanged — every
     * caller above DiskManager keeps passing page_id_t exactly as today.
     */
    void writePage(page_id_t page_id, const char* data);

    /*
     * Reads exactly PAGE_SIZE bytes from offset (page_id * PAGE_SIZE) into
     * `data`. Uses pread(). `data` must point to a PAGE_SIZE buffer (i.e.
     * Page::data_). Throws on I/O error or if page_id >= next_page_id_.
     *
     * PENDING: same VPID->PPID translation note as writePage() above.
     */
    void readPage(page_id_t page_id, char* data);

    /*
     * Returns a page_id for a fresh, writable page.
     * Allocation order:
     *   1. Pop from free_pages_ if non-empty (reuse deallocated space).
     *   2. Otherwise, return next_page_id_++ and extend the file.
     * Does NOT zero-initialise the page bytes — caller must treat the
     * contents as undefined and initialise before writing.
     *
     * PENDING: once PageDirectory exists, this becomes two steps — get a PPID
     * from the physical freelist (unchanged logic above), then
     * directory_.Allocate(ppid) to bind it to a fresh VPID slot and return
     * the VPID instead of the PPID.
     */
    page_id_t allocatePage();

    /*
     * Marks page_id as free for future reuse. Inserts into free_pages_.
     * Does NOT zero the bytes on disk; the page is simply available for
     * reallocation. Caller must ensure no live references remain before
     * calling this (BufferPoolManager must have evicted the page first).
     *
     * PENDING: once PageDirectory exists, `page_id` here is a VPID —
     * directory_.Translate() it to a PPID first, directory_.Deallocate(vpid)
     * to free the VPID slot and bump its generation, then push the PPID onto
     * the existing physical freelist exactly as today.
     */
    void deallocatePage(page_id_t page_id);

    /*
     * Returns the number of pages currently allocated (including free pages
     * that haven't been reclaimed yet). Useful for testing and benchmarking.
     */
    page_id_t getPageCount() const;

private:
    int fd_;
	GlobalMetadata global_metadata_; 

	//Writes global_metadata_ into page 0 (master)
	//defined in .h b/c I suspect this will be used by many other classes
	void UpdateMetadata(){
		ssize_t w = pwrite(fd_, &global_metadata_, PAGE_SIZE, 0);
		HandleWriteError(w, 0); 
		//optional for durability
		fsync(fd_); 
	}   
	void HandleWriteError(ssize_t bytes_written, page_id_t page_id);
	void HandleReadError(ssize_t bytes_read, page_id_t page_id);
    //directory pages... 
        //fixed number of directory pages... dependent on RAM limits
        //1GB ram to allocate -> 2^30 bytes, ram 
        //page_size = 4096 -> 2^12 bytes per page, page_size
            //2^30 / 2^12 = 2^18 pages can be allocated
            //sizeof(page_id_t) = 2^2
            //directory page is VPID/PPID = 2 * sizeof(page_id_t) per allocated physical page = 2^3
            //2^21 bytes needed for ids of allocated pages for 1GB ram, 4KB page
            //2^21 / 2^12 = 2^9 directory pages needed = 
                // (2 * size_of(page_id_t) * ram) / page_size^2 = 2rs / p^2 
    //when a page is deallocated, DELETE its VPID entry, add to freelist
    //when a page is allocated, ADD VPID entry, return VPID not PPID
    //
    // DESIGN NOTES (resolved during design review, not yet implemented):
    //
    // 1. Indirection alone does NOT fix stale references — recycling a VPID slot
    //    the moment its PPID is freed just moves the ABA bug up one layer. Fix:
    //    tag each directory slot with a generation counter, bumped on every
    //    recycle. Callers that need staleness DETECTION (not just translation)
    //    capture (vpid, generation) and check it later; everyone else keeps
    //    passing bare page_id_t exactly as today (see b_plus_tree.h note near
    //    BPlusTreeIterator — that's the one live case of a cached-across-a-gap
    //    reference in this codebase right now).
    //
    // 2. Fixed directory size == fixed max DB size (2rs/p^2 above assumes a 1GB
    //    cap). Accept that cap explicitly for v1, or design for directory growth
    //    (indirect-block style) later — don't silently paper over it.
    //
    // 3. REVISED — no on-disk directory pages at all, and no chicken-and-egg
    //    fixed-physical-address problem to solve. The directory is entirely
    //    in-memory (std::vector<DirEntry>, sized per the 2rs/p^2 math above)
    //    and is thrown away and REBUILT on every open (see the constructor's
    //    doc comment above for the exact algorithm: walk the persisted
    //    freelist chain to get free PPIDs, then pread just the header of
    //    every other PPID to recover its (vpid, generation)). This only works
    //    because SlottedPageHeader::page_id (slotted_page.h) already makes
    //    every live page self-describing — see the note there. Freelist_Page
    //    deliberately does NOT need the same treatment (see freelist_page.h)
    //    since the chain walk in step 1 already accounts for every free PPID.
    //
    // 4. Because of (3), directory writes cost nothing extra on the hot path:
    //    a page's own (vpid, generation) rides along inside the write you're
    //    already doing when you write that page's header (SlottedPage::init()
    //    et al.) — no separate directory I/O, no extra fsync, unlike what an
    //    on-disk directory would have required.
    //
    // 5. VPID recycling needs its own free-slot tracker, separate from the
    //    existing PPID freelist (freelist_head/Freelist_Page above). Since
    //    directory slots are a fixed-size array, an in-memory free-index stack
    //    is a better fit than reusing the chained Freelist_Page format.
    //
    // 6. New class, not bolted directly into DiskManager: PageDirectory, owned
    //    by DiskManager (member, like global_metadata_). allocatePage()/
    //    deallocatePage()/readPage()/writePage() keep their current signatures
    //    (still page_id_t in/out) — internally they translate VPID->PPID via
    //    the directory before touching pread/pwrite. Nothing above DiskManager
    //    (BufferPoolManager, BPlusTree) needs to change.
};
