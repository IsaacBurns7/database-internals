// After Chapters 1-3: Page Manager
// You understand file formats, page layouts, slotted pages. You build the lowest layer — raw I/O abstraction. Nothing smart yet, no caching.

//This is simpler than it sounds. writePage is essentially pwrite(fd, data, PAGE_SIZE, page_id * PAGE_SIZE). The interesting decision here is your page header format — what metadata lives in the first N bytes of every page (page type, free space pointer, slot count). You'll design this from Chapter 3 and it constrains everything above it, so think carefully.
#ifndef DISKMANAGER 
#define DISKMANAGER

#include <string>
#include <unordered_set>
#include <cstdint>
#include <limits>

using page_id_t = uint32_t;
static constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();

struct PageHeader{
	page_id_t page_id; 
	page_id_t next_page_id; 
	uint32_t free_space; 
};

class DiskManager {
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

#endif
