#include <gtest/gtest.h>
#include <filesystem>
#include <cstring>
#include "storage/disk_manager.h"

namespace fs = std::filesystem;

class DiskManagerTest : public ::testing::Test {
protected:
    const std::string test_db = "test_disk_manager.db";

    void SetUp() override {
        // Ensure we start with a fresh file
        if (fs::exists(test_db)) fs::remove(test_db);
    }

    void TearDown() override {
        // Cleanup after test
        if (fs::exists(test_db)) fs::remove(test_db);
    }
};

// 1. Test Basic Page Read and Write
TEST_F(DiskManagerTest, ReadWritePageTest) {
    DiskManager dm(test_db);
    
    // Allocate a page (should be page 1, since 0 is metadata)
    page_id_t page_id = dm.allocatePage();
    EXPECT_EQ(page_id, 1);

    char write_content[PAGE_SIZE];
    char read_content[PAGE_SIZE];
    std::memset(write_content, 'A', PAGE_SIZE);
    std::memset(read_content, 0, PAGE_SIZE);

    // Write content and read it back
    dm.writePage(page_id, write_content);
    dm.readPage(page_id, read_content);

    EXPECT_EQ(std::memcmp(write_content, read_content, PAGE_SIZE), 0);
}

// 2. Test Persistence (Closing and Re-opening)
TEST_F(DiskManagerTest, PersistenceTest) {
    {
        DiskManager dm(test_db);
        dm.allocatePage(); // ID 1
        dm.allocatePage(); // ID 2
        EXPECT_EQ(dm.getPageCount(), 3); // 0 (meta) + 2 data pages
    } // Destructor called here

    // Re-open the same file
    DiskManager dm2(test_db);
    EXPECT_EQ(dm2.getPageCount(), 3);
    
    // New allocation should resume from 3
    EXPECT_EQ(dm2.allocatePage(), 3);
}

// 3. Test Freelist (Deallocation and Reallocation)
TEST_F(DiskManagerTest, FreelistTest) {
    DiskManager dm(test_db);
    
    page_id_t p1 = dm.allocatePage(); // 1
    page_id_t p2 = dm.allocatePage(); // 2
    
    // Deallocate page 1
    dm.deallocatePage(p1);
    
    // The next allocation should reuse p1 instead of creating page 3
    page_id_t p3 = dm.allocatePage();
    EXPECT_EQ(p3, p1);
    EXPECT_EQ(dm.getPageCount(), 3); // Total count shouldn't have grown
}

// 4. Test Error Handling (Read out of bounds)
TEST_F(DiskManagerTest, OutOfBoundsRead) {
    DiskManager dm(test_db);
    char buffer[PAGE_SIZE];
    
    // Page 1 hasn't been allocated yet
    EXPECT_THROW(dm.readPage(1, buffer), std::runtime_error);
}
