#include <vector>
#include "common/macros.h"
#include "storage/disk_manager.h"
#include "storage/disk_scheduler.h"

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // Spawn the background thread
  background_thread_.emplace([&] { StartWorkerThread(); });
}

DiskScheduler::~DiskScheduler() {
  // Put a `std::nullopt` in the queue to signal to exit the loop
  request_queue_.Put(std::nullopt);
  if (background_thread_.has_value()) {
    background_thread_->join();
  }
}

/**
 * @brief Schedules a request for the DiskManager to execute.
 *
 * @param requests The requests to be scheduled.
 */
void DiskScheduler::Schedule(std::vector<DiskRequest> &requests) {
    for(size_t i = 0;i < requests.size(); ++i){
        request_queue_.Put(std::optional<DiskRequest>(std::move(requests[i])));
    }
}

void DiskScheduler::Schedule_Single(DiskRequest &request){
    request_queue_.Put(std::optional<DiskRequest>(std::move(request)));
}

/**
 * @brief Background worker thread function that processes scheduled requests.
 *
 * The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
 * return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
 *
 * Every alternative of the DiskRequest variant is handled here (not just Read/Write) so that Allocate/Deallocate
 * also go through disk_manager_ exclusively from this one thread — see DeallocatePage()'s doc comment in
 * disk_scheduler.h for why that matters.
 */
void DiskScheduler::StartWorkerThread() {
    while(true){
        auto req = request_queue_.Get();
        if(!req.has_value()){
            return;
        }
        std::visit([this](auto &r){
            using T = std::decay_t<decltype(r)>;
            try{
                if constexpr(std::is_same_v<T, ReadRequest>){
                    disk_manager_->readPage(r.page_id, r.data);
                    r.done.set_value();
                }else if constexpr(std::is_same_v<T, WriteRequest>){
                    disk_manager_->writePage(r.page_id, r.data);
                    r.done.set_value();
                }else if constexpr(std::is_same_v<T, AllocateRequest>){
                    r.done.set_value(disk_manager_->allocatePage());
                }else if constexpr(std::is_same_v<T, DeallocateRequest>){
                    disk_manager_->deallocatePage(r.page_id);
                    r.done.set_value();
                }
            }catch(...){
                r.done.set_exception(std::current_exception());
            }
        }, *req);
    }
}

/**
 * @brief Deallocates a page on disk by routing a DeallocateRequest through request_queue_.
 *
 * Blocks the caller until the background worker thread has processed the request, keeping this access serialized
 * with any in-flight readPage/writePage on disk_manager_.
 */
void DiskScheduler::DeallocatePage(page_id_t page_id) {
    std::promise<void> done;
    auto future = done.get_future();
    std::vector<DiskRequest> requests;
    requests.push_back(DeallocateRequest{page_id, std::move(done)});
    Schedule(requests);
    future.get();
}