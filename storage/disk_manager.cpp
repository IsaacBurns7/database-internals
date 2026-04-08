#include "disk_manager.h"
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

/*
 * Internal data
 *	int                           fd_;
    page_id_t                     next_page_id_;
    std::unordered_set<page_id_t> free_pages_;
 */

/*
     * Opens (or creates) the database file at file_path.
     * On creation, initialises the file with a header page at offset 0.
     * On open, reads next_page_id_ from the header so allocation resumes
     * where it left off. Throws std::runtime_error if the file cannot be
     * opened or if the header is corrupt.
     */
DiskManager::DiskManager(const std::string& file_path){
	// try{
	// 	fs::path p = file_path;
	// 	fs::create_directories(p.parent_path());
	//
	// 	ofstream outfile(p);
	// 	if(outfile){
	// 		cout << "Database File successfully created at: " << p << endl; 
	// 	}
	// }catch(const fs::filesystem_error& e){
	// 	cerr << "Error: " << e.what() << endl; 
	// }
	int fd = open(file_path.c_str(), O_WRONLY || O_CREAT | O_TRUNC, 0644);

	if(fd == -1){
		throw std::runtime_error("Error opening file");
	}
	cout << "File descriptor: " << fd << endl; 
	
	ssize_t bytesRead = read(fd, &global_metadata_, sizeof(GlobalMetadata));
	if(bytesRead == -1){
		throw std::runtime_error("Could not read from file");
	}else if(bytesRead < sizeof(GlobalMetadata)){
		throw std::runtime_error("Could not read entire global header from file");  
	}
	if(global_metadata_.magic_number != MAGIC_NUMBER){ //uint32_t
		throw std::runtime_error("Magic number from global header wrong");
	}

	fd_ = fd; 
}

    /*
     * Closes the file descriptor. Does NOT flush any caller-held buffers —
     * BufferPoolManager must flush all dirty pages before destroying this.
     */
DiskManager::~DiskManager(){
	close(fd_); 
}

    /*
     * Writes exactly PAGE_SIZE bytes from `data` to the file at:
     *   offset = page_id * PAGE_SIZE
     * Uses pwrite() so position is not shared with concurrent readers.
     * Caller must ensure `data` points to at least PAGE_SIZE bytes.
     * Throws on I/O error. Does NOT call fsync — durability is LogManager's job.
     */
void DiskManager::writePage(page_id_t page_id, const char* data){
	
}

    /*
     * Reads exactly PAGE_SIZE bytes from offset (page_id * PAGE_SIZE) into
     * `data`. Uses pread(). `data` must point to a PAGE_SIZE buffer (i.e.
     * Page::data_). Throws on I/O error or if page_id >= next_page_id_.
     */
    // void readPage(page_id_t page_id, char* data);

    /*
     * Returns a page_id for a fresh, writable page.
     * Allocation order:
     *   1. Pop from free_pages_ if non-empty (reuse deallocated space).
     *   2. Otherwise, return next_page_id_++ and extend the file.
     * Does NOT zero-initialise the page bytes — caller must treat the
     * contents as undefined and initialise before writing.
     */
    // page_id_t allocatePage();

    /*
     * Marks page_id as free for future reuse. Inserts into free_pages_.
     * Does NOT zero the bytes on disk; the page is simply available for
     * reallocation. Caller must ensure no live references remain before
     * calling this (BufferPoolManager must have evicted the page first).
     */
    // void deallocatePage(page_id_t page_id);

    /*
     * Returns the number of pages currently allocated (including free pages
     * that haven't been reclaimed yet). Useful for testing and benchmarking.
     */
    // page_id_t getPageCount() const;

    /*
     * Writes next_page_id_ and free_pages_ into the header page (page_id 0)
     * so the allocator state survives a clean shutdown. Called by destructor.
     * Not called on every allocatePage() — that would be too expensive.
     */
    // void flushHeader();
    // void readHeader();
