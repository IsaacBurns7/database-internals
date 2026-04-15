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
 	GlobalMetadata global_metadata_; 
 */


/*
     * Opens (or creates) the database file at file_path.
     * On creation, initialises the file with a header page at offset 0.
     * On open, reads next_page_id_ from the header so allocation resumes
     * where it left off. Throws std::runtime_error if the file cannot be
     * opened or if the header is corrupt.
     */
DiskManager::DiskManager(const std::string& file_path){
	//so if there doesnt exist a file_path then what to do ? 
	//Initialize a new one at file_path
	// int fd = open(file_path.c_str(), O_RDWR || O_CREAT | O_TRUNC, 0644);
	int fd = open(file_path.c_str(), O_RDWR | O_CREAT, 0644);
	if(fd == -1){
		throw std::runtime_error("Error opening file: " + std::string(strerror(errno)));
	}
	// cout << "File descriptor: " << fd << endl;
	fd_ = fd; 
	struct stat st;
	fstat(fd, &st);

	if(st.st_size == 0){
		//brand new file 
		global_metadata_.magic_number = MAGIC_NUMBER;
		global_metadata_.next_page_id = 1;
		global_metadata_.freelist_head = INVALID_PAGE_ID;

		UpdateMetadata();
	}else if(st.st_size < sizeof(GlobalMetadata)){
		throw std::runtime_error("File is too small to be a valid database!");
	}else{
		ssize_t bytesRead = read(fd, &global_metadata_, sizeof(GlobalMetadata));
		if(bytesRead < (ssize_t)sizeof(GlobalMetadata)){
			throw std::runtime_error("File is too small to contain a valid global header");
		}
		if(global_metadata_.magic_number != MAGIC_NUMBER){ //uint32_t
			throw std::runtime_error("Magic number mismatch! This isn't a database file or it's corrupt.");
		}
	}


}

    /*
     * Closes the file descriptor. Does NOT flush any caller-held buffers —
     * BufferPoolManager must flush all dirty pages before destroying this.
     */
DiskManager::~DiskManager(){
	UpdateMetadata();
	close(fd_); 
}

    /*
     * Writes exactly PAGE_SIZE bytes from `data` to the file at:
     *   offset = page_id * PAGE_SIZE
     * Uses pwrite() so position is not shared with concurrent readers.
     * Caller must ensure `data` points to at least PAGE_SIZE bytes.
     * Throws on I/O error. Does NOT call fsync — durability is LogManager's job.
     *	- yeah not so sure about not throwing fsync	
	 */
void DiskManager::writePage(page_id_t page_id, const char* data){
	if (page_id >= global_metadata_.next_page_id) {
        throw std::runtime_error("Attempted to write to unallocated page: " + std::to_string(page_id));
    }
	size_t offset = page_id * PAGE_SIZE; 
	ssize_t bytes_written = pwrite(fd_, data, PAGE_SIZE, offset); 
	if(bytes_written != PAGE_SIZE){
		if(bytes_written == -1){
			throw std::system_error(errno, std::generic_category(), "Critical: pwrite failed on page " + std::to_string(page_id)); 
		} else{
			throw std::runtime_error("Partial write occured: wrote " + std::to_string(bytes_written) + " of " + std::to_string(PAGE_SIZE) + " on page " + std::to_string(page_id));
		}
	}
}

    /*
     * Reads exactly PAGE_SIZE bytes from offset (page_id * PAGE_SIZE) into
     * `data`. Uses pread(). `data` must point to a PAGE_SIZE buffer (i.e.
     * Page::data_). Throws on I/O error or if page_id >= next_page_id_.
     */
void DiskManager::readPage(page_id_t page_id, char* data){
	if(page_id >= global_metadata_.next_page_id){
		throw std::runtime_error("Page read out of allowed range");
	}
	size_t offset = page_id * PAGE_SIZE;
	ssize_t bytes_read = pread(fd_, data, PAGE_SIZE, offset);
	if(bytes_read != PAGE_SIZE){
		if(bytes_read == -1){
			throw std::system_error(errno, std::generic_category(), "Critical: pread failed on page " + std::to_string(page_id)); 
		}else if(bytes_read == 0){
			throw std::runtime_error("Read past EOF");
		}else{
			throw std::runtime_error("Partial read: Page is likely truncated or corrupted");
		}
	}
}

    /*
     * Returns a page_id for a fresh, writable page.
     * Allocation order:
     *   1. Find next free in freelist (reuse deallocated space).
     *   2. Otherwise, return next_page_id_++ and extend the file.
     * Does NOT zero-initialise the page bytes — caller must treat the
     * contents as undefined and initialise before writing.
	 * - current implementation, look back and verify this is a good idea
	 *
	 *   CURRENTLY USES MANUAL BUFFER, REFACTOR WHEN BUFFERPOOL IS CREATED
	 *   broken_comment(); 
     */
page_id_t DiskManager::allocatePage(){
	if(global_metadata_.freelist_head == INVALID_PAGE_ID){
		//no freelist, just make new id manually 
		page_id_t new_id = global_metadata_.next_page_id++; 
		UpdateMetadata(); 
		return new_id; 
	}
	//manual read b/c no buffer pool 
	uint8_t buffer[PAGE_SIZE];
	ssize_t r = pread(fd_, buffer, PAGE_SIZE, global_metadata_.freelist_head * PAGE_SIZE);
	HandleReadError(r, global_metadata_.freelist_head); 

	auto* f_page = reinterpret_cast<Freelist_Page*>(buffer);
	page_id_t recycled_id; 

	if(f_page->current_id_count==0){
		recycled_id = global_metadata_.freelist_head; //old head is allocated page  
		global_metadata_.freelist_head = f_page->next_freelist_page; //freelist head is next freelist page
	}else{
		recycled_id = f_page->free_page_ids[--f_page->current_id_count]; //decrement 
		ssize_t w = pwrite(fd_, buffer, PAGE_SIZE, global_metadata_.freelist_head * PAGE_SIZE); //buffer and page point to same val
		HandleWriteError(w, global_metadata_.freelist_head);
	}
	UpdateMetadata();
	return recycled_id; 
}


    /*
     * Does NOT zero the bytes on disk; the page is simply available for
     * reallocation. Caller must ensure no live references remain before
     * calling this (BufferPoolManager must have evicted the page first).
     */
void DiskManager::deallocatePage(page_id_t page_id){
	page_id_t head_id = global_metadata_.freelist_head;
	if(head_id == INVALID_PAGE_ID){
		//no head -> make head 
		Freelist_Page new_f_page; 
		new_f_page.current_id_count = 0;
		new_f_page.next_freelist_page = INVALID_PAGE_ID;
		ssize_t w = pwrite(fd_, &new_f_page, PAGE_SIZE, page_id * PAGE_SIZE);
		HandleWriteError(w, page_id); 
		global_metadata_.freelist_head = page_id;
		UpdateMetadata();
		return; 
	}
	//read in current head 
	Freelist_Page f_page; 
	ssize_t r = pread(fd_, &f_page, PAGE_SIZE, head_id * PAGE_SIZE); 
	HandleReadError(r, head_id); 
	if(f_page.current_id_count == Freelist_Page::MAX_FREE_IDS){
		//Head is full - deallocated page is new freelist head 
		Freelist_Page new_head; 
		new_head.current_id_count = 0;
		new_head.next_freelist_page = head_id;	
		ssize_t w = pwrite(fd_, &new_head, PAGE_SIZE, page_id * PAGE_SIZE);
		HandleWriteError(w, page_id);
		global_metadata_.freelist_head = page_id;  
		UpdateMetadata();
	}else{
		//Head has room - just add deallocated page to freelist 
		f_page.free_page_ids[f_page.current_id_count++] = page_id;
		ssize_t w = pwrite(fd_, &f_page, PAGE_SIZE, head_id * PAGE_SIZE);
		HandleWriteError(w, head_id);
	}
}

    /*
     * Returns the number of pages currently allocated (including free pages
     * that haven't been reclaimed yet). Useful for testing and benchmarking.
     */
page_id_t DiskManager::getPageCount() const{
	return global_metadata_.next_page_id; //[0, next_page_id-1] are allocated 
}


void DiskManager::HandleWriteError(ssize_t bytes_written, page_id_t page_id){
	if(bytes_written == PAGE_SIZE){
		fsync(fd_);
		return;
	}
	
	if(bytes_written == -1){
		throw std::system_error(errno, std::generic_category(), "Critical pwrite failure on Page " + std::to_string(page_id));
	}else{
		// Partial write - the "Torn Page" scenario
		throw std::runtime_error("Partial write on Page " + std::to_string(page_id) + ": Wrote " + std::to_string(bytes_written) + " bytes.");
	}
}

void DiskManager::HandleReadError(ssize_t bytes_read, page_id_t page_id) {
	if (bytes_read == PAGE_SIZE) return; // Perfect read

	if (bytes_read == -1) {
		throw std::system_error(errno, std::generic_category(), 
			"Critical pread failure on Page " + std::to_string(page_id));
	} else {
		// Short read (likely hit EOF prematurely)
		throw std::runtime_error("Incomplete read on Page " + std::to_string(page_id) + 
			": Got " + std::to_string(bytes_read) + " bytes.");
	}
}

