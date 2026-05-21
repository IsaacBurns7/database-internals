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
}
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

Chapters 1-3 + DiskManager: 15-25 hours
Reading is fast, the implementation is small. Most of the time goes into deciding your page format and getting your slotted page layout right. You'll probably rewrite your header struct once.
Chapter 4 + B+ Tree: 40-60 hours
This is the bulk of the project. Splits are tricky, merges are trickier, and getting range scan correct with sibling pointers takes longer than expected. Budget for at least one full debugging session where everything seems right but isn't. If you've never implemented a tree with disk-backed nodes before, lean toward 60.
Chapters 5-8 + Buffer Pool + WAL: 30-45 hours
The buffer pool itself is maybe 10 hours. WAL and recovery take the rest. The WAL invariant is conceptually simple but the implementation has a lot of fiddly ordering constraints that surface as rare bugs.
Integration + cleanup: 10-15 hours
Wiring the dependency chain, writing a basic benchmark, fixing the bugs that only appear end-to-end.

dbms/
├── CMakeLists.txt
├── README.md
│
├── include/
│   ├── storage/
│   │   ├── disk_manager.h
│   │   ├── page.h
│   │   └── slotted_page.h
│   ├── buffer/
│   │   ├── buffer_pool_manager.h
│   │   ├── lru_k_replacer.h
│   │   └── frame.h
│   ├── index/
│   │   ├── b_plus_tree.h
│   │   ├── b_plus_tree_page.h
│   │   ├── internal_page.h
│   │   └── leaf_page.h
│   ├── log/
│   │   ├── log_manager.h
│   │   └── log_record.h
│   ├── engine/
│   │   └── storage_engine.h
│   └── common/
│       ├── config.h        // PAGE_SIZE, POOL_SIZE, etc
│       ├── types.h         // page_id_t, frame_id_t, lsn_t, KeyType, ValueType
│       └── macros.h        // DISALLOW_COPY, ASSERT, etc
│
├── src/
│   ├── storage/
│   │   ├── disk_manager.cpp
│   │   └── slotted_page.cpp
│   ├── buffer/
│   │   ├── buffer_pool_manager.cpp
│   │   └── lru_k_replacer.cpp
│   ├── index/
│   │   ├── b_plus_tree.cpp
│   │   ├── internal_page.cpp
│   │   └── leaf_page.cpp
│   ├── log/
│   │   ├── log_manager.cpp
│   │   └── log_record.cpp
│   └── engine/
│       └── storage_engine.cpp
│
└── test/
    ├── storage/
    │   └── disk_manager_test.cpp
    ├── buffer/
    │   ├── buffer_pool_manager_test.cpp
    │   └── lru_k_replacer_test.cpp
    ├── index/
    │   └── b_plus_tree_test.cpp
    ├── log/
    │   └── log_manager_test.cpp
    └── engine/
        └── storage_engine_test.cpp

# EXTENSION
What's missing from the plan:
1. Concurrency (biggest gap)
Your plan has zero mention of it. Add to the B+ tree phase:

Latch crabbing for tree traversal (read latches on the way down, upgrade to write latch only at the target node)
A std::shared_mutex per page frame in the buffer pool

This alone makes the project HFT-relevant because it demonstrates you understand the read/write contention problem.
2. Benchmark directory
Add bench/ alongside test/. At minimum:

sequential_write_bench.cpp — saturate write throughput
random_read_bench.cpp — measure buffer pool hit rate under Zipfian distribution
mixed_workload_bench.cpp — 80% reads, 20% writes, report ops/sec

Put the actual numbers in your README. Interviewers remember "450k ops/sec on a 10GB dataset with 95% buffer pool hit rate" — they don't remember "I built a DBMS."
3. LRU-K Replacer is already in your plan — good
Most students do plain LRU. LRU-K is the right choice and shows you've read the literature. Make sure your README explains why (frequency vs recency, handles sequential scan flooding).
4. One performance feature to add to config.h
Add O_DIRECT as a compile-time flag in your DiskManager so you can bypass the OS page cache and measure the difference. Even just benchmarking with and without it and documenting the results is impressive.

What's already great:

Slotted pages show you understand variable-length records
WAL invariant enforcement in evict() is exactly the right place and you have it right
The phased build order (correctness first, then caching) is the correct approach
Directory structure is clean and professional

Revised time estimates if you add concurrency:

Add ~15-20 hours to the B+ tree phase for latch crabbing
Add ~10 hours to buffer pool for frame-level latching and testing under concurrent load

## Build Plan

### Phase 1 — DiskManager + project setup
**Apr 21 – May 9 (2.5 weeks) · Pre-internship**

- Read Database Internals Ch. 1–3 (page formats, slotted pages)
- Bootstrap CMake project, write `types.h`, `config.h`, `macros.h`
- Implement DiskManager: `readPage`, `writePage`, `allocatePage`, `deallocatePage`
- Design slotted page header — this constrains everything above it, take your time
- Write `disk_manager_test.cpp` with edge cases (page 0, last page, reuse)

> This phase has the most uninterrupted time you'll have. Use it. A clean DiskManager now saves 10 hours of refactoring later.

---

### Phase 2 — B+ Tree (correctness only)
**May 9 – May 25 (2.5 weeks) · Pre-internship**

- Read Ch. 4 (B-trees, splits, merges)
- Implement leaf page and internal page layouts on top of DiskManager directly
- `get()`, `insert()`, and `scan()` first — no `remove()` yet, splits are enough
- Validate sibling pointers via `scan()` before moving on — bugs hide here
- Leave `remove()` and merge logic for Phase 4 — don't block on it now

> You likely won't finish the full B+ tree before internship starts. That's fine — get insert and scan solid, leave merge for the summer.

---

### Phase 3 — B+ Tree (remove + merge)
**May 26 – Jun 20 (~4 weeks) · Light pace**

- Weekends only for the first 2 weeks — let yourself settle into the internship
- Implement `remove()`, `mergeOrRedistribute()` — expect one long debugging session
- Write `b_plus_tree_test.cpp`: random insert/delete sequences, verify scan output
- By end of June: a fully correct, disk-backed key-value store (slow, no cache)

> Weeks 1–2 of internship drain more energy than expected. Even 2 hours on Saturday keeps momentum without burning you out.

---

### Phase 4 — Buffer Pool + LRU-K
**Jun 21 – Jul 12 (~3 weeks) · Light pace**

- Read Ch. 5 (buffer management, replacement policies)
- Implement LRU-K replacer — track last K accesses per frame, evict lowest frequency
- Implement BufferPoolManager: `fetchPage`, `unpinPage`, `newPage`, `flushPage`, `evict()`
- Wire B+ tree to go through BufferPoolManager instead of DiskManager directly
- Add frame-level `std::shared_mutex` for read/write latching

---

### Phase 5 — WAL + Recovery
**Jul 13 – Aug 3 (~3 weeks) · Light pace**

- Read Ch. 8 (WAL, ARIES recovery)
- Implement LogManager: `appendLog()`, `flush(lsn)`, `recover()`
- Enforce WAL invariant in `evict()`: flush log before writing dirty page
- Add group commit: batch fsync calls, measure throughput improvement
- Wire into StorageEngine, run recovery test (kill mid-write, verify correctness)

> WAL ordering bugs are subtle and rare. Write a crash-simulation test early — don't save testing for the end.

---

### Phase 6 — Benchmarks + polish
**Aug 4 – Aug 14 (~1.5 weeks) · Post-internship sprint**

- Write `bench/`: sequential write, random read (Zipfian), mixed 80/20 workload
- Measure buffer pool hit rate, ops/sec, p99 latency — put real numbers in README
- Profile with `perf stat` or `valgrind --tool=cachegrind`, fix one hot spot
- Add `O_DIRECT` flag to DiskManager, benchmark with vs without OS page cache
- Write a clean README: architecture diagram, design decisions, benchmark results

---

### Recruiting prep
**Aug 15 onwards**

- Finalize resume — project goes on with real benchmark numbers
- Refresh leetcode: 2 mediums/day, focus on trees, graphs, dynamic programming
- Prep DBMS project walkthrough — know every design decision cold
- Start applications: HFT (Jane Street, Citadel, Hudson River), FAANG, quant firms
