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
 * TODO(P1): Add implementation
 *
 * @brief Schedules a request for the DiskManager to execute.
 *
 * @param requests The requests to be scheduled.
 */
void DiskScheduler::Schedule(std::vector<DiskRequest> &requests) {
    for(size_t i = 0;i < requests.size(); ++i){
        request_queue_.Put(std::optional<DiskRequest>(std::move(requests[i])));
    }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Background worker thread function that processes scheduled requests.
 *
 * The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
 * return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
 */
// PENDING (thread-safety): once DiskRequest grows an Allocate/Deallocate
// kind (see the struct's note in disk_scheduler.h), this loop needs a branch
// for them too, calling disk_manager_->allocatePage()/deallocatePage() from
// here instead of DeallocatePage() (and a future NewPage-equivalent) calling
// disk_manager_ directly off the queue. That's what actually gets alloc/
// dealloc onto the same single-threaded serialization Read/Write already get.
void DiskScheduler::StartWorkerThread() {
    while(true){
        auto req = request_queue_.Get();
        if(!req.has_value()){
            return;
        }
        try{
            switch(req->request_kind_){
                case RequestKind::write:
                    disk_manager_->writePage(req->page_id_, req->data_);
                    break;
                case RequestKind::read:
                    disk_manager_->readPage(req->page_id_, req->data_);
                    break;
            }
            req->callback_.set_value(true);
        }catch(...){
            req->callback_.set_exception(std::current_exception());
        }
    }
}