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
     */
    void writePage(page_id_t page_id, const char* data);

    /*
     * Reads exactly PAGE_SIZE bytes from offset (page_id * PAGE_SIZE) into
     * `data`. Uses pread(). `data` must point to a PAGE_SIZE buffer (i.e.
     * Page::data_). Throws on I/O error or if page_id >= next_page_id_.
     */
    void readPage(page_id_t page_id, char* data);

    /*
     * Returns a page_id for a fresh, writable page.
     * Allocation order:
     *   1. Pop from free_pages_ if non-empty (reuse deallocated space).
     *   2. Otherwise, return next_page_id_++ and extend the file.
     * Does NOT zero-initialise the page bytes — caller must treat the
     * contents as undefined and initialise before writing.
     */
    page_id_t allocatePage();

    /*
     * Marks page_id as free for future reuse. Inserts into free_pages_.
     * Does NOT zero the bytes on disk; the page is simply available for
     * reallocation. Caller must ensure no live references remain before
     * calling this (BufferPoolManager must have evicted the page first).
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
};
