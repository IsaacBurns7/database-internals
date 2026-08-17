#pragma once

#include <future>  // NOLINT
#include <optional>
#include <thread>  // NOLINT
#include <vector>
#include <variant>

#include "common/channel.h"
#include "storage/disk_manager.h"

/**
 * @brief Represents a Read, Write, Allocate, or Deallocate request for the DiskManager to execute.
 *
 * Each alternative carries its own promise type because the result differs by kind: Read/Write/Deallocate just
 * signal completion (std::promise<void>), while Allocate hands back the freshly allocated page_id_t.
 */
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
  void Schedule_Single(DiskRequest &request); 

  void StartWorkerThread();

  /**
   * @brief Deallocates a page on disk.
   *
   * Note: You should look at the documentation for `DeletePage` in `BufferPoolManager` before using this method.
   * Pin-count gating (making sure nobody still has the page pinned) is NOT this function's job — that's checked in
   * BufferPoolManager::DeletePage() before it ever calls this method.
   *
   * Builds a DeallocateRequest and routes it through request_queue_ like a read/write, so this call blocks the
   * caller's thread until the background worker thread has processed it. That keeps every disk_manager_ access
   * serialized through the one worker thread instead of racing with an in-flight readPage/writePage — DiskManager
   * itself has no internal locking (see its class doc comment). Same reasoning applies to allocation, but
   * BufferPoolManager::NewPage() doesn't call through here yet (see its own note in buffer_pool_manager.cpp) — a
   * related but distinct bug.
   *
   * @param page_id The page ID of the page to deallocate from disk.
   */
  void DeallocatePage(page_id_t page_id);

 private:
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));
  /** A shared queue to concurrently schedule and process requests. When the DiskScheduler's destructor is called,
   * `std::nullopt` is put into the queue to signal to the background thread to stop execution. */
  Channel<std::optional<DiskRequest>> request_queue_;
  /** The background thread responsible for issuing scheduled requests to the disk manager. */
  std::optional<std::thread> background_thread_;
};