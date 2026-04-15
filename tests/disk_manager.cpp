#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include "storage/disk_manager.h"

class DiskManagerTest : public ::testing::Test {
protected:
    const std::string db_name = "test_garbage.db";
	void SetUp() override {
		std::remove(db_name.c_str()); // Nukes the file before the test starts
	}
    void TearDown() override {
        std::remove(db_name.c_str());
    }

    // Helper to see if the file actually has data
    size_t GetFileSize() {
        std::ifstream in(db_name, std::ifstream::ate | std::ifstream::binary);
        return in.tellg();
    }
};

// TEST 1: The "Why did you delete my data?" test
TEST_F(DiskManagerTest, ConstructorDestroysExistingFile) {
    // 1. Create a "dummy" database file with actual data
    {
        std::ofstream ofs(db_name, std::ios::binary);
        ofs << "This is important production data. Do not delete!";
        ofs.flush();
    } 
    ASSERT_GT(GetFileSize(), 0);

    // 2. Try to open it with your DiskManager. 
    // We expect it to throw because O_TRUNC wipes the magic number,
    // and your constructor throws when it can't read the magic number.
    try {
        DiskManager dm(db_name);
    } catch (const std::runtime_error& e) {
        // We caught the error! Now we check the "crime scene."
        std::cout << "[EXPECTED ERROR CAUGHT]: " << e.what() << std::endl;
    } catch (...) {
        FAIL() << "DiskManager threw an unknown exception type!";
    }

    // 3. THE CRUEL VERDICT:
    // If your constructor used O_TRUNC, the file size will be 0.
    // A database manager should NEVER truncate an existing file on open.
    EXPECT_NE(GetFileSize(), 0) 
        << "CRITICAL FAILURE: DiskManager truncated an existing file to 0 bytes! "
        << "All data was lost during the constructor call.";
}

// TEST 2: The "Fresh Start" test
TEST_F(DiskManagerTest, InitializesNewFileCorrectly) {
    // 1. Ensure file doesn't exist (SetUp handled this, but let's be paranoid)
    std::remove(db_name.c_str());

    // 2. This should NOT throw now. It should see size 0 and write a header.
    ASSERT_NO_THROW({
        DiskManager dm(db_name);
    });

    // 3. Verify the DiskManager actually created a file of PAGE_SIZE
    EXPECT_EQ(GetFileSize(), PAGE_SIZE) 
        << "The DiskManager should have written a full PAGE_SIZE header to the new file.";
    
    // 4. Re-open to see if it persists
    {
        DiskManager dm(db_name);
        EXPECT_EQ(dm.getPageCount(), 1); // Only the header page exists
    }
}

TEST_F(DiskManagerTest, StrictAllocationAndWriteTest) {
    DiskManager dm(db_name);
    char write_buf[PAGE_SIZE];
    char read_buf[PAGE_SIZE];
    
    // 1. THE ILLEGAL WRITE: Try to write to page 1 before allocating it.
    // This MUST throw if you implemented the safety check correctly.
    std::memset(write_buf, 'X', PAGE_SIZE);
    EXPECT_THROW(dm.writePage(1, write_buf), std::runtime_error) 
        << "Security Breach: DiskManager allowed writing to an unallocated page!";

    // 2. THE BIRTH: Allocate page 1 and page 2.
    page_id_t p1 = dm.allocatePage(); // Should be 1
    page_id_t p2 = dm.allocatePage(); // Should be 2
    EXPECT_EQ(p1, 1);
    EXPECT_EQ(p2, 2);

    // 3. THE VALID WRITE: Now write to them.
    std::memset(write_buf, 'A', PAGE_SIZE);
    ASSERT_NO_THROW(dm.writePage(p1, write_buf));
    
    std::memset(write_buf, 'B', PAGE_SIZE);
    ASSERT_NO_THROW(dm.writePage(p2, write_buf));

    // 4. THE PERSISTENCE: Read back and verify they haven't bled into each other.
    dm.readPage(p1, read_buf);
    EXPECT_EQ(read_buf[0], 'A');
    
    dm.readPage(p2, read_buf);
    EXPECT_EQ(read_buf[0], 'B');

    // 5. THE RECYCLING: Deallocate p1 and re-allocate it.
    // This ensures that writePage works even when the page_id comes from the freelist.
    dm.deallocatePage(p1);
    page_id_t p3 = dm.allocatePage(); 
    EXPECT_EQ(p3, p1) << "Failure: allocatePage did not recycle the ID from the freelist.";

    std::memset(write_buf, 'C', PAGE_SIZE);
    ASSERT_NO_THROW(dm.writePage(p3, write_buf));
    
    dm.readPage(p3, read_buf);
    EXPECT_EQ(read_buf[0], 'C');
}

// TEST 4: The Freelist Disaster
TEST_F(DiskManagerTest, DeallocateAndReallocate) {
    // Setup valid file
    FILE* f = fopen(db_name.c_str(), "wb");
    GlobalMetadata meta;
    meta.magic_number = MAGIC_NUMBER;
    meta.next_page_id = 1; 
    meta.freelist_head = INVALID_PAGE_ID;
    fwrite(&meta, sizeof(meta), 1, f);
    fclose(f);

    DiskManager dm(db_name);
    
    // 1. Allocate a page (should be page 1)
    page_id_t p1 = dm.allocatePage();
    EXPECT_EQ(p1, 1);

    // 2. Deallocate it. 
    // CRITICAL: Your code has a `pread` where a `pwrite` should be here.
    ASSERT_NO_THROW(dm.deallocatePage(p1));

    // 3. Reallocate. Should get page 1 back from the freelist.
    page_id_t p2 = dm.allocatePage();
    EXPECT_EQ(p2, p1) << "Freelist failed to recycle the page. Are you leaking disk space?";
}

// TEST 5: Out of Bounds
TEST_F(DiskManagerTest, ReadOutOfBounds) {
    FILE* f = fopen(db_name.c_str(), "wb");
    GlobalMetadata meta;
    meta.magic_number = MAGIC_NUMBER;
    meta.next_page_id = 2; 
    fwrite(&meta, sizeof(meta), 1, f);
    fclose(f);

    DiskManager dm(db_name);
    char buf[PAGE_SIZE];
    
    // Page 2 hasn't been allocated yet (next_page_id is 2)
    EXPECT_THROW(dm.readPage(2, buf), std::runtime_error);
}
