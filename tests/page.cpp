#include <gtest/gtest.h>
#include <cstring>
#include "storage/page.h"
#include "common/config.h" // Assuming PAGE_SIZE and INVALID_PAGE_ID are here

namespace my_project {

TEST(PageTest, InitialState) {
    Page page;
    // Test default constructor values
    EXPECT_EQ(page.getPageId(), INVALID_PAGE_ID);
    EXPECT_EQ(page.getPinCount(), 0);
    EXPECT_FALSE(page.isDirty());
    EXPECT_EQ(page.getLSN(), INVALID_LSN);
}

TEST(PageTest, DataBufferAccess) {
    Page page;
    char *data = page.getData();
    
    // Ensure the buffer is actually writable
    const char *test_str = "AggieData";
    std::memcpy(data, test_str, std::strlen(test_str) + 1);
    
    EXPECT_STREQ(page.getData(), "AggieData");
}

TEST(PageTest, SettersAndGetters) {
    Page page;
    
    page.setDirty(true);
    EXPECT_TRUE(page.isDirty());
    
    page.setLSN(42);
    EXPECT_EQ(page.getLSN(), 42);
    
    // Note: page_id_ and pin_count_ don't have public setters in your snippet
    // as they are typically managed internally by BufferPoolManager.
    // If you add them later, test them here.
}

TEST(PageTest, ResetTest) {
    Page page;
    
    // 1. Manually "dirty" the metadata (you might need to add friend class 
    // or setters if these are private, but based on your reset() logic):
    page.setDirty(true);
    page.setLSN(100);
    
    // 2. Reset the page
    page.reset();
    
    // 3. Verify metadata is wiped, but data buffer remains (per your comments)
    EXPECT_FALSE(page.isDirty());
    EXPECT_EQ(page.getLSN(), INVALID_LSN);
    EXPECT_EQ(page.getPageId(), INVALID_PAGE_ID);
    EXPECT_EQ(page.getPinCount(), 0);
}

} // namespace my_project
