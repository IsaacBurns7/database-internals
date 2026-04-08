#include "page.h"

/*
 * Page — in-memory container for one PAGE_SIZE-byte disk block.
 *
 * Responsibilities:
 *   - Owns the raw byte buffer that DiskManager reads into and writes from.
 *   - Carries identity (page_id) so callers never have to track it separately.
 *   - Carries buffer-pool bookkeeping: dirty flag and pin count.
 *     The buffer pool sets/clears these; Page just stores them.
 *   - Stores the page's LSN (log sequence number) so BufferPoolManager::evict()
 *     can enforce the WAL invariant: flush the log up to this LSN before
 *     writing the page to disk.
 *
 * What Page deliberately does NOT do:
 *   - It does not interpret the bytes in data_[]. That is SlottedPage's job.
 *   - It does not perform I/O. That is DiskManager's job.
 *   - It does not manage its own lifecycle. That is BufferPoolManager's job.
 *
 * Layout in memory:
 *   data_[PAGE_SIZE]  — the raw block, exactly as it lives on disk.
 *   Everything else   — ephemeral metadata, never written to disk.
 */


/*
 * Internal data
 *  char      data_[PAGE_SIZE];
    page_id_t page_id_   = INVALID_PAGE_ID;
    lsn_t     lsn_       = INVALID_LSN;
    int       pin_count_ = 0;
    bool      is_dirty_  = false;
 */

/*
 * Returns a pointer to the raw PAGE_SIZE byte buffer.
 * DiskManager writes directly into this pointer via readPage().
 * SlottedPage wraps this pointer to interpret the layout.
 * Callers must not read past PAGE_SIZE bytes.
 */
char* Page::getData(){
	return data_;
}
const char* Page::getData() const {
	return data_;
}

    /*
     * Returns the page_id assigned when this frame was loaded.
     * INVALID_PAGE_ID (typically UINT32_MAX) means the frame is empty/free.
     * Set by BufferPoolManager when it pins a page into this frame.
     */
page_id_t Page::getPageId() const {
	return page_id_; 
}

    /*
     * Dirty flag — true if in-memory data differs from what is on disk.
     * Set to true by callers (BPlusTree, SlottedPage) when they mutate data_.
     * Cleared by BufferPoolManager after a successful flush.
     * BufferPoolManager::evict() checks this before deciding whether to write.
     */
bool Page::isDirty() const {
	return is_dirty_;
}
void Page::setDirty(bool dirty){
	is_dirty_ = dirty;
}

    /*
     * Pin count — number of threads currently holding a reference to this page.
     * BufferPoolManager increments this on fetchPage() and decrements on unpinPage().
     * A frame with pin_count > 0 is ineligible for eviction.
     * Reaching 0 makes the frame a candidate for the LRU-K replacer.
     */
int Page::getPinCount() const {
	return pin_count_;
}

    /*
     * LSN (log sequence number) of the most recent log record that modified
     * this page. Written by LogManager when it appends a log record.
     * Read by BufferPoolManager::evict() to enforce:
     *   log_manager->flush(page->getLSN()) before pwrite(page->getData()).
     * If the page has never been modified, LSN is INVALID_LSN (0).
     */
lsn_t Page::getLSN() const {
	return lsn_ ;
}
void Page::setLSN(lsn_t lsn) {
	lsn_ = lsn;
}

/*
 * Resets all metadata to defaults. Called by BufferPoolManager when it
 * recycles a frame for a different page_id. Does NOT zero data_[] —
 * the next readPage() overwrites it entirely anyway.
 */
void Page::reset() {
	is_dirty_ = false; 
	pin_count_ = 0;
    page_id_ = INVALID_PAGE_ID;
    lsn_ = INVALID_LSN;
}

   
