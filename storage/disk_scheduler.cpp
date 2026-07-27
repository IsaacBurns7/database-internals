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
void DiskScheduler::StartWorkerThread() {
    while(true){
        auto req = request_queue_.Get();
        if(!req.has_value()){
            return;
        }
        try{
            if(req->is_write_){
                disk_manager_->writePage(req->page_id_, req->data_);
            }else{
                disk_manager_->readPage(req->page_id_, req->data_);
            }
            req->callback_.set_value(true);
        }catch(...){
            req->callback_.set_exception(std::current_exception());
        }
    }
}