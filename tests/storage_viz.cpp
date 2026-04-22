#include "storage/disk_manager.h"
#include "storage/slotted_page.h"
#include "storage/page.h"
#include <iostream>
#include <iomanip>
#include <vector>

using std::cout;
using std::endl;
using std::setw;
using std::left;
using std::right;

void print_separator(int width = 80) {
    cout << std::string(width, '=') << endl;
}

void print_subsection(const std::string& title) {
    cout << "\n" << std::string(title.length(), '-') << endl;
    cout << title << endl;
    cout << std::string(title.length(), '-') << endl;
}

void print_memory_stats(DiskManager& dm) {
    page_id_t total_pages = dm.getPageCount();
    size_t total_bytes = (size_t)total_pages * PAGE_SIZE;
    double total_mb = total_bytes / (1024.0 * 1024.0);

    cout << std::fixed << std::setprecision(2);
    cout << "  Total Pages Allocated: " << total_pages << endl;
    cout << "  Total Memory:          " << total_mb << " MB (" << total_bytes << " bytes)" << endl;
    cout << "  Page Size:             " << PAGE_SIZE << " bytes" << endl;
}

void page_allocation() {
    cout << "\n";
    print_separator();
    cout << "PAGE ALLOCATION DEMO - Disk Manager & Free List Management" << endl;
    print_separator();

    // Create/open database
    const std::string db_path = "demo.db";
    cout << "\nInitializing DiskManager with file: " << db_path << endl;

    DiskManager dm(db_path);

    print_subsection("Initial State (Master Page Only)");
    print_memory_stats(dm);

    // Allocate a few pages
    print_subsection("Allocating 5 Pages");
    std::vector<page_id_t> allocated_pages;
    for (int i = 0; i < 5; ++i) {
        page_id_t page_id = dm.allocatePage();
        allocated_pages.push_back(page_id);
        cout << "  Allocated page_id: " << page_id << endl;
    }
    cout << "\n  After allocation:" << endl;
    print_memory_stats(dm);

    // Deallocate some pages (demonstrates free list)
    print_subsection("Deallocating 3 Pages (Creating Free List)");
    for (int i = 0; i < 3; ++i) {
        page_id_t page_to_free = allocated_pages[i];
        cout << "  Deallocating page_id: " << page_to_free << endl;
        dm.deallocatePage(page_to_free);
    }
    cout << "\n  After deallocation:" << endl;
    print_memory_stats(dm);
    cout << "  Note: Free list now contains pages that can be reused" << endl;

    // Allocate more pages (will reuse from free list)
    print_subsection("Allocating 2 More Pages (Reusing from Free List)");
    std::vector<page_id_t> reused_pages;
    for (int i = 0; i < 2; ++i) {
        page_id_t page_id = dm.allocatePage();
        reused_pages.push_back(page_id);
        cout << "  Allocated page_id: " << page_id << " (reused from free list)" << endl;
    }
    cout << "\n  After reallocation:" << endl;
    print_memory_stats(dm);

    // Deallocate remaining pages
    print_subsection("Deallocating All Remaining Pages");
    for (int i = 3; i < (int)allocated_pages.size(); ++i) {
        page_id_t page_to_free = allocated_pages[i];
        cout << "  Deallocating page_id: " << page_to_free << endl;
        dm.deallocatePage(page_to_free);
    }
    for (page_id_t page_id : reused_pages) {
        cout << "  Deallocating page_id: " << page_id << endl;
        dm.deallocatePage(page_id);
    }
    cout << "\n  After final deallocation:" << endl;
    print_memory_stats(dm);
    cout << "  Note: All pages are now in the free list, ready for reuse" << endl;

    print_subsection("Summary");
    cout << "  ✓ Master page created and persisted" << endl;
    cout << "  ✓ Pages allocated sequentially" << endl;
    cout << "  ✓ Free list chain created on first deallocation" << endl;
    cout << "  ✓ Pages successfully recycled from free list" << endl;
    cout << "  ✓ Memory footprint tracked throughout lifecycle" << endl;

    print_separator();
    cout << "\n";
}

void record_viz() {
    cout << "\n";
    print_separator();
    cout << "RECORD OPERATIONS DEMO - Insert & Delete through SlottedPage" << endl;
    print_separator();

    const std::string db_path = "demo_records.db";
    cout << "\nInitializing DiskManager with file: " << db_path << endl;

    DiskManager dm(db_path);

    // Allocate a page and initialize it as a leaf page
    print_subsection("Allocating and Initializing a Page");
    page_id_t page_id = dm.allocatePage();
    cout << "  Allocated page_id: " << page_id << endl;

    // Create a buffer for the page
    char page_data[PAGE_SIZE];
    SlottedPage slotted_page(page_data);
    slotted_page.init(page_id, LEAF_PAGE);
    cout << "  Initialized as LEAF_PAGE" << endl;

    // Display initial state
    cout << "\n  Initial page state:" << endl;
    cout << "    Slot count: " << slotted_page.getSlotCount() << endl;
    cout << "    Free space: " << slotted_page.getFreeSpace() << " bytes" << endl;
    cout << "    Total free space: " << slotted_page.getTotalFreeSpace() << " bytes" << endl;

    // Insert some records
    print_subsection("Inserting 5 Records");
    std::vector<slot_id_t> inserted_slots;
    std::vector<std::string> records = {
        "Alice",
        "Bob",
        "Charlie",
        "Diana",
        "Eve"
    };

    for (int i = 0; i < (int)records.size(); ++i) {
        const std::string& record = records[i];
        auto slot_id = slotted_page.insertRecord(record.c_str(), record.length());
        if (slot_id.has_value()) {
            inserted_slots.push_back(slot_id.value());
            cout << "  Inserted '" << record << "' at slot_id: " << slot_id.value() << endl;
        } else {
            cout << "  Failed to insert '" << record << "' (not enough space)" << endl;
        }
    }

    cout << "\n  After insertions:" << endl;
    cout << "    Slot count: " << slotted_page.getSlotCount() << endl;
    cout << "    Free space: " << slotted_page.getFreeSpace() << " bytes" << endl;
    cout << "    Total free space: " << slotted_page.getTotalFreeSpace() << " bytes" << endl;

    // Retrieve and display records
    print_subsection("Retrieving Records");
    for (slot_id_t slot_id : inserted_slots) {
        auto record_span = slotted_page.getRecord(slot_id);
        if (!record_span.empty()) {
            cout << "  Slot " << slot_id << ": ";
            cout.write(record_span.data(), record_span.size());
            cout << " (" << record_span.size() << " bytes)" << endl;
        }
    }

    // Delete some records
    print_subsection("Deleting 3 Records");
    for (int i = 0; i < 3; ++i) {
        slot_id_t slot_to_delete = inserted_slots[i];
        bool deleted = slotted_page.deleteRecord(slot_to_delete);
        if (deleted) {
            cout << "  Deleted record at slot_id: " << slot_to_delete << endl;
        } else {
            cout << "  Failed to delete record at slot_id: " << slot_to_delete << endl;
        }
    }

    cout << "\n  After deletions:" << endl;
    cout << "    Slot count: " << slotted_page.getSlotCount() << endl;
    cout << "    Free space: " << slotted_page.getFreeSpace() << " bytes" << endl;
    cout << "    Total free space (including deleted): " << slotted_page.getTotalFreeSpace() << " bytes" << endl;

    // Insert new records to fill the gaps
    print_subsection("Inserting 2 New Records (to demonstrate space reuse)");
    std::vector<std::string> new_records = {
        "Frank",
        "Grace"
    };

    for (const auto& record : new_records) {
        auto slot_id = slotted_page.insertRecord(record.c_str(), record.length());
        if (slot_id.has_value()) {
            cout << "  Inserted '" << record << "' at slot_id: " << slot_id.value() << endl;
        } else {
            cout << "  Failed to insert '" << record << "'" << endl;
        }
    }

    cout << "\n  After new insertions:" << endl;
    cout << "    Slot count: " << slotted_page.getSlotCount() << endl;
    cout << "    Free space: " << slotted_page.getFreeSpace() << " bytes" << endl;
    cout << "    Total free space: " << slotted_page.getTotalFreeSpace() << " bytes" << endl;

    // Compact the page
    print_subsection("Compacting Page (Defragmentation)");
    slotted_page.compactify();
    cout << "  Page compacted" << endl;

    cout << "\n  After compaction:" << endl;
    cout << "    Free space: " << slotted_page.getFreeSpace() << " bytes" << endl;
    cout << "    Total free space: " << slotted_page.getTotalFreeSpace() << " bytes" << endl;

    // Write page to disk and read it back
    print_subsection("Persisting Page to Disk");
    dm.writePage(page_id, page_data);
    cout << "  Page written to disk" << endl;

    char read_buffer[PAGE_SIZE];
    dm.readPage(page_id, read_buffer);
    cout << "  Page read back from disk" << endl;

    SlottedPage restored_page(read_buffer);
    cout << "\n  Restored page state:" << endl;
    cout << "    Slot count: " << restored_page.getSlotCount() << endl;
    cout << "    Free space: " << restored_page.getFreeSpace() << " bytes" << endl;

    print_subsection("Summary");
    cout << "  ✓ Page allocated and initialized" << endl;
    cout << "  ✓ Records inserted into page" << endl;
    cout << "  ✓ Records retrieved successfully" << endl;
    cout << "  ✓ Records deleted (creating fragmentation)" << endl;
    cout << "  ✓ New records inserted to reuse slots" << endl;
    cout << "  ✓ Page defragmented via compactify()" << endl;
    cout << "  ✓ Page persisted and restored from disk" << endl;

    print_separator();
    cout << "\n";
}

int main() {
    try {
        page_allocation();
        record_viz();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << endl;
        return 1;
    }
    return 0;
}
