#include "storage/page_guard.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "buffer/arc_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/disk_scheduler.h"

// ReadPageGuard/WritePageGuard only ever hand out valid instances through BufferPoolManager
// (their constructors are private, friended only to it). Since CheckedReadPage/CheckedWritePage
// are still TODO(P1) and would abort/throw, these tests go through the MakeTestReadGuard /
// MakeTestWriteGuard / SetTestFrame* static helpers added to BufferPoolManager so that the guards
// themselves can be exercised in isolation, independent of the rest of the buffer pool.
class PageGuardTest : public ::testing::Test {
 protected:
  const std::string db_name = "test_page_guard.db";

  void SetUp() override {
    // DiskScheduler spawns a background worker thread, and several tests below are
    // EXPECT_DEATH-based. fork()-based death tests are unsafe once other threads exist, so force
    // the safer (slower) "threadsafe" style instead of gtest's default "fast".
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    std::remove(db_name.c_str());
    disk_manager_ = std::make_unique<DiskManager>(db_name);
    disk_scheduler_ = std::make_shared<DiskScheduler>(disk_manager_.get());
    replacer_ = std::make_shared<ArcReplacer>(kNumFrames);
    bpm_latch_ = std::make_shared<std::mutex>();
  }

  void TearDown() override {
    disk_scheduler_.reset();
    disk_manager_.reset();
    std::remove(db_name.c_str());
  }

  static constexpr size_t kNumFrames = 8;

  auto MakeFrame(frame_id_t frame_id) -> std::shared_ptr<FrameHeader> { return std::make_shared<FrameHeader>(frame_id); }

  std::unique_ptr<DiskManager> disk_manager_;
  std::shared_ptr<DiskScheduler> disk_scheduler_;
  std::shared_ptr<ArcReplacer> replacer_;
  std::shared_ptr<std::mutex> bpm_latch_;
};

// ============================================================================
// Basic accessors
// ============================================================================

TEST_F(PageGuardTest, ReadGuardExposesPageId) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(42, frame, replacer_, bpm_latch_, disk_scheduler_);
  EXPECT_EQ(guard.GetPageId(), 42);
}

TEST_F(PageGuardTest, WriteGuardExposesPageId) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestWriteGuard(7, frame, replacer_, bpm_latch_, disk_scheduler_);
  EXPECT_EQ(guard.GetPageId(), 7);
}

TEST_F(PageGuardTest, WriteGuardMutationIsVisibleThroughReadGuardOnSameFrame) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 2);

  {
    auto wguard = BufferPoolManager::MakeTestWriteGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
    std::memcpy(wguard.GetDataMut(), "hello", 6);
  }  // drops, releasing the exclusive lock so the read guard below can take it shared.

  auto rguard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  EXPECT_STREQ(rguard.GetData(), "hello");
  EXPECT_STREQ(rguard.As<char>(), "hello");
}

TEST_F(PageGuardTest, IsDirtyReflectsFrameState) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  EXPECT_FALSE(guard.IsDirty());

  BufferPoolManager::SetTestFrameDirty(frame, true);
  EXPECT_TRUE(guard.IsDirty());
}

// ============================================================================
// Locking semantics
// ============================================================================

TEST_F(PageGuardTest, MultipleReadGuardsCanCoexistOnSameFrame) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 2);

  auto guard1 = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  auto guard2 = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);

  EXPECT_EQ(guard1.GetPageId(), 1);
  EXPECT_EQ(guard2.GetPageId(), 1);
}

TEST_F(PageGuardTest, WriteGuardBlocksConcurrentWriteGuardOnSameFrame) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 2);
  replacer_->RecordAccess(0, 1);

  auto guard1 = BufferPoolManager::MakeTestWriteGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);

  std::atomic<bool> acquired{false};
  std::thread t([&] {
    auto guard2 = BufferPoolManager::MakeTestWriteGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
    acquired.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_FALSE(acquired.load()) << "second WritePageGuard acquired the frame while the first was still alive";

  guard1.Drop();
  t.join();
  EXPECT_TRUE(acquired.load());
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_F(PageGuardTest, MoveConstructorInvalidatesSource) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard1 = BufferPoolManager::MakeTestReadGuard(9, frame, replacer_, bpm_latch_, disk_scheduler_);
  ReadPageGuard guard2(std::move(guard1));

  EXPECT_EQ(guard2.GetPageId(), 9);
  EXPECT_DEATH(guard1.GetPageId(), "");
}

TEST_F(PageGuardTest, WriteGuardMoveConstructorInvalidatesSource) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard1 = BufferPoolManager::MakeTestWriteGuard(9, frame, replacer_, bpm_latch_, disk_scheduler_);
  WritePageGuard guard2(std::move(guard1));

  EXPECT_EQ(guard2.GetPageId(), 9);
  EXPECT_DEATH(guard1.GetPageId(), "");
}

TEST_F(PageGuardTest, MoveAssignmentDropsPreviousResource) {
  auto frame_a = MakeFrame(0);
  auto frame_b = MakeFrame(1);
  BufferPoolManager::SetTestFramePinCount(frame_a, 1);
  BufferPoolManager::SetTestFramePinCount(frame_b, 1);
  replacer_->RecordAccess(0, 100);

  auto guard_a = BufferPoolManager::MakeTestReadGuard(100, frame_a, replacer_, bpm_latch_, disk_scheduler_);
  auto guard_b = BufferPoolManager::MakeTestReadGuard(200, frame_b, replacer_, bpm_latch_, disk_scheduler_);

  guard_a = std::move(guard_b);

  EXPECT_EQ(guard_a.GetPageId(), 200);
  // Dropping guard_a's original resource (frame_a) should have decremented its pin count and,
  // since that was the last pin, marked frame 0 evictable in the replacer.
  EXPECT_EQ(BufferPoolManager::GetTestFramePinCount(frame_a), 0);
  EXPECT_EQ(replacer_->Size(), 1);
}

TEST_F(PageGuardTest, SelfMoveAssignmentIsSafe) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(5, frame, replacer_, bpm_latch_, disk_scheduler_);
  auto &guard_ref = guard;
  guard = std::move(guard_ref);

  EXPECT_EQ(guard.GetPageId(), 5);
}

// ============================================================================
// Drop / pin count / evictable interaction
// ============================================================================

TEST_F(PageGuardTest, DropDecrementsPinCountAndMarksEvictableWhenLastPin) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);
  replacer_->RecordAccess(0, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  guard.Drop();

  EXPECT_EQ(BufferPoolManager::GetTestFramePinCount(frame), 0);
  EXPECT_EQ(replacer_->Size(), 1);
}

TEST_F(PageGuardTest, DropDoesNotMarkEvictableWhileOtherPinsRemain) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 2);  // simulates one other outstanding pin
  replacer_->RecordAccess(0, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  guard.Drop();

  EXPECT_EQ(BufferPoolManager::GetTestFramePinCount(frame), 1);
  EXPECT_EQ(replacer_->Size(), 0);
}

TEST_F(PageGuardTest, DoubleDropIsIdempotent) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);
  replacer_->RecordAccess(0, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  guard.Drop();
  guard.Drop();  // must be a no-op; must not double-decrement pin_count_ or double-unlock.

  EXPECT_EQ(BufferPoolManager::GetTestFramePinCount(frame), 0);
  EXPECT_EQ(replacer_->Size(), 1);
}

TEST_F(PageGuardTest, DestructorDropsTheGuard) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);
  replacer_->RecordAccess(0, 1);

  {
    auto guard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
    (void)guard;
  }

  EXPECT_EQ(BufferPoolManager::GetTestFramePinCount(frame), 0);
  EXPECT_EQ(replacer_->Size(), 1);
}

TEST_F(PageGuardTest, WriteGuardDropDecrementsPinCountAndMarksEvictable) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);
  replacer_->RecordAccess(0, 1);

  auto guard = BufferPoolManager::MakeTestWriteGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  guard.Drop();

  EXPECT_EQ(BufferPoolManager::GetTestFramePinCount(frame), 0);
  EXPECT_EQ(replacer_->Size(), 1);
}

// ============================================================================
// Invalid-guard usage (death tests)
// ============================================================================

TEST_F(PageGuardTest, UsingDroppedReadGuardAborts) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestReadGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  guard.Drop();

  EXPECT_DEATH(guard.GetPageId(), "");
  EXPECT_DEATH(guard.GetData(), "");
  EXPECT_DEATH(guard.IsDirty(), "");
}

TEST_F(PageGuardTest, UsingDroppedWriteGuardAborts) {
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestWriteGuard(1, frame, replacer_, bpm_latch_, disk_scheduler_);
  guard.Drop();

  EXPECT_DEATH(guard.GetPageId(), "");
  EXPECT_DEATH(guard.GetDataMut(), "");
  EXPECT_DEATH(guard.IsDirty(), "");
}

TEST_F(PageGuardTest, DefaultConstructedGuardsAreInvalid) {
  ReadPageGuard read_guard;
  WritePageGuard write_guard;

  EXPECT_DEATH(read_guard.GetPageId(), "");
  EXPECT_DEATH(write_guard.GetPageId(), "");
}

// ============================================================================
// Flush
// ============================================================================

TEST_F(PageGuardTest, WriteGuardFlushWritesDataAndClearsDirtyFlag) {
  page_id_t page_id = disk_manager_->allocatePage();
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 1);

  auto guard = BufferPoolManager::MakeTestWriteGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_);
  std::memset(guard.GetDataMut(), 'Z', PAGE_SIZE);
  BufferPoolManager::SetTestFrameDirty(frame, true);

  guard.Flush();
  EXPECT_FALSE(guard.IsDirty());

  char read_buf[PAGE_SIZE];
  disk_manager_->readPage(page_id, read_buf);

  char expected[PAGE_SIZE];
  std::memset(expected, 'Z', PAGE_SIZE);
  EXPECT_EQ(std::memcmp(read_buf, expected, PAGE_SIZE), 0);
}

TEST_F(PageGuardTest, ReadGuardFlushWritesUnderlyingFrameData) {
  page_id_t page_id = disk_manager_->allocatePage();
  auto frame = MakeFrame(0);
  BufferPoolManager::SetTestFramePinCount(frame, 2);

  {
    auto wguard = BufferPoolManager::MakeTestWriteGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_);
    std::memset(wguard.GetDataMut(), 'Q', PAGE_SIZE);
  }
  BufferPoolManager::SetTestFrameDirty(frame, true);

  auto rguard = BufferPoolManager::MakeTestReadGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_);
  rguard.Flush();
  EXPECT_FALSE(rguard.IsDirty());

  char read_buf[PAGE_SIZE];
  disk_manager_->readPage(page_id, read_buf);

  char expected[PAGE_SIZE];
  std::memset(expected, 'Q', PAGE_SIZE);
  EXPECT_EQ(std::memcmp(read_buf, expected, PAGE_SIZE), 0);
}

// ============================================================================
// Known bug: see the BUGS section in storage/page_guard.h.
//
// ReadPageGuard::Flush() mutates frame_->is_dirty_ (a plain, non-atomic bool) while holding only
// a *shared* lock on the frame's rwlatch_. Multiple threads are allowed to hold ReadPageGuards on
// the same frame simultaneously and each is free to call Flush(), so this is a real data race
// under the C++ memory model (best observed with ThreadSanitizer; a plain run will not
// necessarily fail since every writer happens to write the same value, `false`). This test
// exercises the racy path; it is expected to run clean without TSAN and is not a regression
// guard by itself.
// ============================================================================

TEST_F(PageGuardTest, ConcurrentReadGuardFlushDoesNotCrash) {
  page_id_t page_id = disk_manager_->allocatePage();
  auto frame = MakeFrame(0);
  constexpr int kNumReaders = 4;
  BufferPoolManager::SetTestFramePinCount(frame, kNumReaders);
  BufferPoolManager::SetTestFrameDirty(frame, true);

  std::vector<std::thread> threads;
  threads.reserve(kNumReaders);
  for (int i = 0; i < kNumReaders; i++) {
    threads.emplace_back([&] {
      auto guard = BufferPoolManager::MakeTestReadGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_);
      guard.Flush();
    });
  }
  for (auto &t : threads) {
    t.join();
  }

  SUCCEED();  // reaching here without crashing/hanging is the point of this test.
}
