After Chapters 1-3: Page Manager
You understand file formats, page layouts, slotted pages. You build the lowest layer — raw I/O abstraction. Nothing smart yet, no caching.
cppclass DiskManager {
public:
    explicit DiskManager(const std::string& file_path);
    
    void writePage(page_id_t page_id, const char* data);  // writes exactly PAGE_SIZE bytes
    void readPage(page_id_t page_id, char* data);          // reads exactly PAGE_SIZE bytes
    page_id_t allocatePage();                              // extends file, returns new page_id
    void deallocatePage(page_id_t page_id);                // marks page free in header

private:
    int fd_;
    page_id_t next_page_id_;
    std::unordered_set<page_id_t> free_pages_;  // reuse deallocated pages
};
This is simpler than it sounds. writePage is essentially pwrite(fd, data, PAGE_SIZE, page_id * PAGE_SIZE). The interesting decision here is your page header format — what metadata lives in the first N bytes of every page (page type, free space pointer, slot count). You'll design this from Chapter 3 and it constrains everything above it, so think carefully.

After Chapter 4: B+ Tree
You now build the tree on top of DiskManager directly — no buffer pool yet, every node access is a raw disk read. This is intentionally naive and you'll fix it in the next phase. The value is getting splits and merges correct before adding caching complexity.
cppclass BPlusTree {
public:
    explicit BPlusTree(DiskManager* disk_manager);

    void insert(KeyType key, ValueType value);
    bool remove(KeyType key);
    std::optional<ValueType> get(KeyType key);

    // range scan — returns all values where key is in [start, end]
    std::vector<ValueType> scan(KeyType start, KeyType end);

private:
    page_id_t root_page_id_;
    DiskManager* disk_manager_;

    // internal helpers
    page_id_t findLeaf(KeyType key);
    void splitChild(page_id_t parent_id, int child_index);
    void mergeOrRedistribute(page_id_t node_id);  // called on underflow after delete
};
The split and merge logic is where you'll spend most of your time. scan is important to implement here because it validates that your leaf-level sibling pointers are correct — a common place for bugs to hide.
At this point you have a functional persistent key-value store. Slow, but correct. That correctness is what you verify before moving on.

After Chapters 5 & 8: Buffer Pool + WAL
Two things happen here and they're coupled. The buffer pool sits between the B+ tree and the disk manager — the tree stops calling DiskManager directly and goes through the buffer pool instead. WAL gets layered on top so you survive crashes.
cppclass BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager, LogManager* log_manager);

    Page* fetchPage(page_id_t page_id);   // brings page into memory, pins it
    void unpinPage(page_id_t page_id, bool is_dirty);
    Page* newPage(page_id_t* out_page_id);  // allocates + fetches a fresh page
    void flushPage(page_id_t page_id);      // force write to disk regardless of dirty flag

private:
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::vector<Page> frames_;
    LRUKReplacer replacer_;   // tracks eviction candidates among unpinned frames
    DiskManager* disk_;
    LogManager* log_;

    frame_id_t evict();  // flushes dirty page if needed, returns free frame
};

class LogManager {
public:
    lsn_t appendLog(LogRecord record);   // returns log sequence number
    void flush(lsn_t up_to_lsn);         // syncs log to disk up to given LSN
    void recover();                       // replays log on startup, redoes committed ops
};
The key constraint that ties these together: a dirty page cannot be flushed to disk before its log record is flushed. That's the WAL invariant. Your BufferPoolManager::evict() is where you enforce it — before writing a dirty page, call log_->flush(page->getLSN()). Get that ordering wrong and your recovery is broken.
After this your B+ tree needs one change — every write operation fetches pages through the buffer pool and logs the modification before marking the page dirty. The tree's external API doesn't change, just its internals.

Integration
At the end you wire them together in a single dependency chain and the external interface simplifies to:
cppclass StorageEngine {
public:
    StorageEngine(const std::string& data_file, const std::string& log_file, size_t buffer_pool_size);
    // calls LogManager::recover() in constructor before accepting operations

    void put(KeyType key, ValueType value);
    std::optional<ValueType> get(KeyType key);
    bool remove(KeyType key);
    std::vector<ValueType> scan(KeyType start, KeyType end);
};
The integration work is mostly making sure the ownership and initialization order is correct — LogManager and DiskManager are constructed first, BufferPoolManager takes pointers to both, BPlusTree takes a pointer to BufferPoolManager. Recovery runs before the tree is usable. That's it.
