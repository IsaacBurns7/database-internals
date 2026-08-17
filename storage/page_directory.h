// storage/page_directory.h
struct DirectoryEntry {
    page_id_t physical_id;   // PPID this VPID currently maps to; INVALID_PAGE_ID if slot is free
    uint32_t  generation;    // bumped every time this slot is recycled
};

class PageDirectory {
public:
    explicit PageDirectory(size_t num_slots);

    page_id_t Allocate(page_id_t physical_id);   // pop a free slot (or bump high-water mark), bind it, bump generation, return VPID
    void Deallocate(page_id_t vpid);             // clear mapping, bump generation, push vpid onto free_slots_
    page_id_t Translate(page_id_t vpid) const;   // VPID -> PPID; throws if vpid is unbound

    uint32_t GetGeneration(page_id_t vpid) const;
    bool IsValid(page_id_t vpid, uint32_t expected_generation) const;

private:
    std::vector<DirectoryEntry> entries_;   // in-memory, sized to num_slots — same reasoning as your 2rs/p^2 math
    std::vector<page_id_t> free_slots_;     // in-memory stack of recyclable VPIDs
    page_id_t next_vpid_ = 0;               // high-water mark, mirrors GlobalMetadata::next_page_id
};
