#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>
#include "storage/slotted_page.h"

class SlottedPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Allocate a clean buffer for each test
        buffer = new char[PAGE_SIZE];
        std::memset(buffer, 0, PAGE_SIZE);
        page = new SlottedPage(buffer);
    }

    void TearDown() override {
        delete[] buffer;
        delete page;
    }

    char* buffer;
    SlottedPage* page;
    const page_id_t TEST_PAGE_ID = 1;
};

// 1. Initialization Test
TEST_F(SlottedPageTest, InitializationTest) {
    page->init(TEST_PAGE_ID, SlottedPageType::LEAF_PAGE);
    
    EXPECT_EQ(page->getPageType(), SlottedPageType::LEAF_PAGE);
    EXPECT_EQ(page->getSlotCount(), 0);
    // Free space should be PAGE_SIZE minus Header size
    EXPECT_GT(page->getFreeSpace(), 0);
    EXPECT_LT(page->getFreeSpace(), PAGE_SIZE);
}

// 2. Basic Insert and Retrieval
TEST_F(SlottedPageTest, InsertAndGetTest) {
    page->init(TEST_PAGE_ID, SlottedPageType::LEAF_PAGE);

    std::string record1 = "Hello, Database!";
    std::string record2 = "Slotted pages are cool.";

    auto slot1 = page->insertRecord(record1.c_str(), record1.length());
    auto slot2 = page->insertRecord(record2.c_str(), record2.length());

    ASSERT_TRUE(slot1.has_value());
    ASSERT_TRUE(slot2.has_value());
    EXPECT_NE(slot1.value(), slot2.value());

    auto res1 = page->getRecord(slot1.value());
    auto res2 = page->getRecord(slot2.value());

    EXPECT_EQ(std::string(res1.data(), res1.size()), record1);
    EXPECT_EQ(std::string(res2.data(), res2.size()), record2);
}

// 3. Deletion Test
TEST_F(SlottedPageTest, DeleteRecordTest) {
    page->init(TEST_PAGE_ID, SlottedPageType::LEAF_PAGE);
    
    std::string data = "Delete me";
    auto slot = page->insertRecord(data.c_str(), data.length());
    ASSERT_TRUE(slot.has_value());

    EXPECT_TRUE(page->deleteRecord(slot.value()));
    
    auto res = page->getRecord(slot.value());
    EXPECT_TRUE(res.empty());
    
    // Deleting already deleted or OOB should be false
    EXPECT_FALSE(page->deleteRecord(slot.value()));
    EXPECT_FALSE(page->deleteRecord(999));
}

// 4. Update Test (Same Size)
TEST_F(SlottedPageTest, UpdateRecordTest) {
    page->init(TEST_PAGE_ID, SlottedPageType::LEAF_PAGE);
    
    std::string old_data = "Original";
    std::string new_data = "Updatedd"; // Same length: 8
    
    auto slot = page->insertRecord(old_data.c_str(), old_data.length());
    bool updated = page->updateRecord(slot.value(), new_data.c_str(), new_data.length());
    
    EXPECT_TRUE(updated);
    auto res = page->getRecord(slot.value());
    EXPECT_EQ(std::string(res.data(), res.size()), new_data);
}

// 5. Compaction Test
TEST_F(SlottedPageTest, CompactionTest) {
    page->init(TEST_PAGE_ID, SlottedPageType::LEAF_PAGE);

    // 1. Insert 3 records
    std::string r1 = "FirstRecord";
    std::string r2 = "SecondRecordToBeDeleted";
    std::string r3 = "ThirdRecord";

    auto s1 = page->insertRecord(r1.data(), r1.length());
    auto s2 = page->insertRecord(r2.data(), r2.length());
    auto s3 = page->insertRecord(r3.data(), r3.length());

    // 2. Delete the middle record to create a gap
    page->deleteRecord(s2.value());
    uint16_t freeBefore = page->getFreeSpace();

    // 3. Force compaction
    page->compactify();

    // 4. Verify data integrity of remaining records
    auto res1 = page->getRecord(s1.value());
    auto res3 = page->getRecord(s3.value());

    EXPECT_EQ(std::string(res1.data(), res1.size()), r1);
    EXPECT_EQ(std::string(res3.data(), res3.size()), r3);
    
    // 5. Contiguous free space should have increased
    EXPECT_GT(page->getFreeSpace(), freeBefore);
}

// 6. Edge Case: Page Full
TEST_F(SlottedPageTest, PageFullTest) {
    page->init(TEST_PAGE_ID, SlottedPageType::LEAF_PAGE);
    
    // Try to insert a record larger than the page
    std::vector<char> large_record(PAGE_SIZE, 'A');
    auto slot = page->insertRecord(large_record.data(), PAGE_SIZE);
    
    EXPECT_FALSE(slot.has_value());
}
