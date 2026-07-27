#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "storage/disk_scheduler.h"
#include "storage/disk_manager.h"

class DiskSchedulerTest : public ::testing::Test {
protected:
    const std::string db_name = "test_disk_scheduler.db";

    void SetUp() override {
        std::remove(db_name.c_str());
    }
    void TearDown() override {
        std::remove(db_name.c_str());
    }
};

TEST_F(DiskSchedulerTest, ConstructAndDestroyDoesNotHang) {
    DiskManager dm(db_name);
    ASSERT_NO_THROW({
        DiskScheduler scheduler(&dm);
    });
}

TEST_F(DiskSchedulerTest, ScheduleWritesThenReadsBackSamePage) {
    DiskManager dm(db_name);
    DiskScheduler scheduler(&dm);

    page_id_t page_id = dm.allocatePage();

    char write_buf[PAGE_SIZE];
    char read_buf[PAGE_SIZE];
    std::memset(write_buf, 'A', PAGE_SIZE);
    std::memset(read_buf, 0, PAGE_SIZE);

    // Write
    {
        auto promise = scheduler.CreatePromise();
        auto future = promise.get_future();
        std::vector<DiskRequest> requests;
        requests.push_back(DiskRequest{true, write_buf, page_id, std::move(promise)});
        scheduler.Schedule(requests);
        EXPECT_TRUE(future.get());
    }

    // Read back
    {
        auto promise = scheduler.CreatePromise();
        auto future = promise.get_future();
        std::vector<DiskRequest> requests;
        requests.push_back(DiskRequest{false, read_buf, page_id, std::move(promise)});
        scheduler.Schedule(requests);
        EXPECT_TRUE(future.get());
    }

    EXPECT_EQ(std::memcmp(write_buf, read_buf, PAGE_SIZE), 0);
}

TEST_F(DiskSchedulerTest, ScheduleBatchOfRequestsInOneCall) {
    DiskManager dm(db_name);
    DiskScheduler scheduler(&dm);

    page_id_t p1 = dm.allocatePage();
    page_id_t p2 = dm.allocatePage();

    char buf1[PAGE_SIZE];
    char buf2[PAGE_SIZE];
    std::memset(buf1, 'X', PAGE_SIZE);
    std::memset(buf2, 'Y', PAGE_SIZE);

    auto promise1 = scheduler.CreatePromise();
    auto future1 = promise1.get_future();
    auto promise2 = scheduler.CreatePromise();
    auto future2 = promise2.get_future();

    std::vector<DiskRequest> requests;
    requests.push_back(DiskRequest{true, buf1, p1, std::move(promise1)});
    requests.push_back(DiskRequest{true, buf2, p2, std::move(promise2)});
    scheduler.Schedule(requests);

    EXPECT_TRUE(future1.get());
    EXPECT_TRUE(future2.get());

    char read1[PAGE_SIZE];
    char read2[PAGE_SIZE];
    dm.readPage(p1, read1);
    dm.readPage(p2, read2);
    EXPECT_EQ(std::memcmp(buf1, read1, PAGE_SIZE), 0);
    EXPECT_EQ(std::memcmp(buf2, read2, PAGE_SIZE), 0);
}

TEST_F(DiskSchedulerTest, ReadOfUnallocatedPagePropagatesExceptionThroughFuture) {
    DiskManager dm(db_name);
    DiskScheduler scheduler(&dm);

    char read_buf[PAGE_SIZE];
    auto promise = scheduler.CreatePromise();
    auto future = promise.get_future();

    // No pages have been allocated yet, so page 5 is out of range and
    // DiskManager::readPage should throw. The worker thread must catch
    // that and forward it through the promise/future instead of dying.
    std::vector<DiskRequest> requests;
    requests.push_back(DiskRequest{false, read_buf, 5, std::move(promise)});
    scheduler.Schedule(requests);

    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(DiskSchedulerTest, DeallocatePageDelegatesToDiskManager) {
    DiskManager dm(db_name);
    DiskScheduler scheduler(&dm);

    page_id_t p1 = dm.allocatePage();
    ASSERT_NO_THROW(scheduler.DeallocatePage(p1));

    page_id_t p2 = dm.allocatePage();
    EXPECT_EQ(p2, p1) << "DeallocatePage should have freed p1 for reuse.";
}
