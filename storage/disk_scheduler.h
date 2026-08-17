#pragma once

#include <future>  // NOLINT
#include <optional>
#include <thread>  // NOLINT
#include <vector>
#include <variant>

#include "common/channel.h"
#include "storage/disk_manager.h"

/**
 * @brief Represents a Write or Read request for the DiskManager to execute.
 */
// PENDING (thread-safety, not the VPID/PPID directory — separate discussion):
// this struct only models Read/Write today (is_write_ as a bool). If
// AllocatePage/DeallocatePage get routed through the same queue as Read/Write
// (see the note on DeallocatePage() below for why that's worth doing), this
// probably needs to become an enum request kind (Read/Write/Allocate/
// Deallocate) instead of a bool, since Allocate doesn't fit "write true/false"
// and its result is a page_id_t, not a bool — callback_ would need to become
// a variant/second promise type for that case (std::promise<page_id_t>),
// since std::promise<bool> can't carry an allocated id back to the caller.
// enum RequestKind{
//     read,
//     write, 
//     allocate, //returns VPID 
//     deallocate //returns true or false 
// };
// struct DiskRequest {
//     RequestKind request_kind_; 
//     char *data_;
//     page_id_t page_id_;
//     std::promise<bool> callback_;
//         /** Callback used to signal to the request issuer when the request has been completed. */
// };

struct ReadRequest{
    page_id_t page_id;
    char *data;
    std::promise<void> done; //wtf is a std::promise<void>
};
struct WriteRequest{
    page_id_t page_id;
    const char *data; 
    std::promise<void> done;
};
struct AllocateRequest{
    std::promise<page_id_t> done;
};
struct DeallocateRequest{
    page_id_t page_id;
    std::promise<void> done;
};
using DiskRequest = std::variant<ReadRequest, WriteRequest,
                                 AllocateRequest, DeallocateRequest>;            
/**
 * @brief The DiskScheduler schedules disk read and write operations.
 *
 * A request is scheduled by calling DiskScheduler::Schedule() with an appropriate DiskRequest object. The scheduler
 * maintains a background worker thread that processes the scheduled requests using the disk manager. The background
 * thread is created in the DiskScheduler constructor and joined in its destructor.
 */
class DiskScheduler {
 public:
  explicit DiskScheduler(DiskManager *disk_manager);
  ~DiskScheduler();

  void Schedule(std::vector<DiskRequest> &requests);

  void StartWorkerThread();

  using DiskSchedulerPromise = std::promise<bool>;

  /**
   * @brief Create a Promise object. If you want to implement your own version of promise, you can change this function
   * so that our test cases can use your promise implementation.
   *
   * @return std::promise<bool>
   */
  auto CreatePromise() -> DiskSchedulerPromise { return {}; };

  /**
   * @brief Deallocates a page on disk.
   *
   * Note: You should look at the documentation for `DeletePage` in `BufferPoolManager` before using this method.
   *
   * @param page_id The page ID of the page to deallocate from disk.
   */
  // PENDING (thread-safety): two separate things worth keeping straight here.
  //
  // 1. Pin-count gating (from the earlier findRecord()/staleness discussion —
  //    see b_plus_tree.cpp's findRecord() note) is NOT this function's job.
  //    "Don't free a page anyone still has pinned" has to be checked in
  //    BufferPoolManager::DeletePage() (buffer_pool_manager.cpp) BEFORE it
  //    ever calls this method — that doc comment above already points there.
  //    By the time DeallocatePage() runs, the pin check has already passed;
  //    this function doesn't need to know pins exist.
  //
  // 2. What this function SHOULD be doing but isn't: it calls
  //    disk_manager_->deallocatePage(page_id) directly on the caller's
  //    thread, bypassing request_queue_ entirely — unlike ReadPage/WritePage,
  //    which only ever touch disk_manager_ from the single background thread
  //    (see StartWorkerThread() in disk_scheduler.cpp). DiskManager itself
  //    has no internal locking (see its class doc comment), so a caller
  //    thread calling this while the background thread is mid readPage/
  //    writePage is an unsynchronized concurrent access to global_metadata_
  //    and fd_ — a real race, independent of anything about pins. Fix: build
  //    a Deallocate DiskRequest (see the struct's note above) and Schedule()
  //    it like a read/write, so every disk_manager_ access is serialized
  //    through the one worker thread. Same reasoning applies to allocation —
  //    BufferPoolManager::NewPage() currently doesn't call through here at
  //    all (see its own note in buffer_pool_manager.cpp), which is a related
  //    but distinct bug.

 private:
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));
  /** A shared queue to concurrently schedule and process requests. When the DiskScheduler's destructor is called,
   * `std::nullopt` is put into the queue to signal to the background thread to stop execution. */
  Channel<std::optional<DiskRequest>> request_queue_;
  /** The background thread responsible for issuing scheduled requests to the disk manager. */
  std::optional<std::thread> background_thread_;
};