#include "buffer/arc_replacer.h"
#include <optional>
#include "common/config.h"
#include <algorithm>
#include <iterator>

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames)
    : mru_size_(0), mfu_size_(0), mru_ghost_size_(0), mfu_ghost_size_(0), replacer_size_(num_frames) {}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<std::pair<page_id_t, frame_id_t>> {
    // scans a list back-to-front for the first evictable frame, removing it from
    // the list (but not from alive_map_) if found.
    auto EvictFrom = [this](std::list<frame_id_t> &lst) -> std::optional<frame_id_t> {
        for (auto it = lst.rbegin(); it != lst.rend(); ++it) {
            frame_id_t candidate = *it;
            auto map_it = alive_map_.find(candidate);
            if (map_it == alive_map_.end()) {
                LogFatalStream(__FILE__, __LINE__) << "frame_id " << candidate << " present in list but missing from alive_map_";
                return std::nullopt;
            }
            if (map_it->second->evictable_) {
                lst.erase(std::next(it).base());
                return candidate;
            }
        }
        return std::nullopt;
    };

    bool prefer_mru = mru_size_ >= mru_target_size_;
    std::optional<frame_id_t> victim;
    bool evicted_from_mru = false;

    if (prefer_mru) {
        victim = EvictFrom(mru_);
        evicted_from_mru = true;
        if (!victim.has_value()) {
            victim = EvictFrom(mfu_);
            evicted_from_mru = false;
        }
    } else {
        victim = EvictFrom(mfu_);
        evicted_from_mru = false;
        if (!victim.has_value()) {
            victim = EvictFrom(mru_);
            evicted_from_mru = true;
        }
    }

    if (!victim.has_value()) {
        return std::nullopt;
    }

    frame_id_t frame_id = victim.value();
    auto map_it = alive_map_.find(frame_id);
    std::shared_ptr<FrameStatus> frame_status = map_it->second;
    page_id_t page_id = frame_status->page_id_;
    alive_map_.erase(map_it);
    curr_size_--;

    if (evicted_from_mru) {
        mru_size_--;
        frame_status->arc_status_ = ArcStatus::MRU_GHOST;
        mru_ghost_.push_front(page_id);
        mru_ghost_size_++;
    } else {
        mfu_size_--;
        frame_status->arc_status_ = ArcStatus::MFU_GHOST;
        mfu_ghost_.push_front(page_id);
        mfu_ghost_size_++;
    }
    frame_status->frame_id_ = INVALID_FRAME_ID;
    ghost_map_[page_id] = frame_status;

    return {page_id, frame_id};
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */


// enum class ArcStatus { MRU, MFU, MRU_GHOST, MFU_GHOST };

// struct FrameStatus {
//   page_id_t page_id_;
//   frame_id_t frame_id_;
//   bool evictable_;
//   ArcStatus arc_status_;
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
    //check if alive, ghost, or non-existent
    auto alive_it = alive_map_.find(frame_id);
    auto ghost_it = ghost_map_.find(page_id);
    bool alive = alive_it != alive_map_.end();
    bool ghost = ghost_it != ghost_map_.end();
    bool non_existent = !(alive || ghost);

    if(alive){
        std::shared_ptr<FrameStatus> frame_status = alive_it->second;
        if(frame_status->arc_status_ == ArcStatus::MRU){
            auto it = std::find(mru_.begin(), mru_.end(), frame_id);
            if(it == mru_.end()){
                LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " marked MRU in alive_map_ but not found in mru_";
                return;
            }
            mru_.erase(it);
            mru_size_--;
        }
        else if(frame_status->arc_status_ == ArcStatus::MFU){
            auto it = std::find(mfu_.begin(), mfu_.end(), frame_id);
            if(it == mfu_.end()){
                LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " marked MFU in alive_map_ but not found in mfu_";
                return;
            }
            mfu_.erase(it);
            mfu_size_--;
        }else{
            //return to avoid pushing to the front when clearly, state is corrupted...
            LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " in alive_map_ has corrupted arc_status_ (neither MRU nor MFU)";
            return;
        }
        frame_status->arc_status_ = ArcStatus::MFU;
        mfu_.push_front(frame_status->frame_id_);
        mfu_size_++;
    }else if(ghost){
        std::shared_ptr<FrameStatus> frame_status = ghost_it->second;
        if(frame_status->arc_status_ == ArcStatus::MRU_GHOST){
            mru_target_size_ += mru_ghost_size_ >= mfu_ghost_size_ ? 1 : mfu_ghost_size_ / mru_ghost_size_;
            auto it = std::find(mru_ghost_.begin(), mru_ghost_.end(), page_id);
            if(it == mru_ghost_.end()){
                LogFatalStream(__FILE__, __LINE__) << "page_id " << page_id << " marked MRU_GHOST in ghost_map_ but not found in mru_ghost_";
                return;
            }
            mru_ghost_.erase(it);
            mru_ghost_size_--;
        }
        else if(frame_status->arc_status_ == ArcStatus::MFU_GHOST){
            size_t decrease = mfu_ghost_size_ >= mru_ghost_size_ ? 1 : mru_ghost_size_ / mfu_ghost_size_;
            if(mru_target_size_ >= decrease)
                mru_target_size_ -= decrease;
            auto it = std::find(mfu_ghost_.begin(), mfu_ghost_.end(), page_id);
            if(it == mfu_ghost_.end()){
                LogFatalStream(__FILE__, __LINE__) << "page_id " << page_id << " marked MFU_GHOST in ghost_map_ but not found in mfu_ghost_";
                return;
            }
            mfu_ghost_.erase(it);
            mfu_ghost_size_--;
        }
        frame_status->frame_id_ = frame_id;
        frame_status->arc_status_ = ArcStatus::MFU;
        frame_status->evictable_ = false;
        ghost_map_.erase(ghost_it);
        alive_map_.emplace(frame_id, frame_status);
        mfu_.push_front(frame_id);
        mfu_size_++;
    }else if(non_existent){
        //? make new frame_status for new guy
        if(mru_ghost_size_ + mru_size_ > replacer_size_){
            LogFatalStream(__FILE__, __LINE__) << "mru_size_ (" << mru_size_ << ") + mru_ghost_size_ (" << mru_ghost_size_ << ") exceeds replacer_size_ (" << replacer_size_ << ")";
        }
        if(mru_size_ + mru_ghost_size_ == replacer_size_){
            mru_ghost_.pop_back();
            mru_ghost_size_--;
        }else if(mru_size_ + mru_ghost_size_ + mfu_size_ + mfu_ghost_size_ == (2 * replacer_size_)){
            mfu_ghost_.pop_back();
            mfu_ghost_size_--;
        }
        mru_.push_front(frame_id);
        mru_size_++;
        alive_map_.emplace(frame_id, std::make_shared<FrameStatus>(page_id, frame_id, /*evictable=*/false, ArcStatus::MRU));

    }else{
        //how did u even end up here... ?
        LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " / page_id " << page_id << " is neither alive, ghost, nor non_existent";
    }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
    if (frame_id >= replacer_size_) {
        LogFatalStream(__FILE__, __LINE__) << "SetEvictable called with invalid frame_id " << frame_id
                                            << " (replacer_size_ is " << replacer_size_ << ")";
        return;
    }

    auto it = alive_map_.find(frame_id);
    if (it == alive_map_.end()) {
        return;
    }

    std::shared_ptr<FrameStatus> frame_status = it->second;
    if (frame_status->evictable_ == set_evictable) {
        return;
    }

    frame_status->evictable_ = set_evictable;
    if (set_evictable) {
        curr_size_++;
    } else {
        curr_size_--;
    }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
    auto it = alive_map_.find(frame_id);
    if (it == alive_map_.end()) {
        return;
    }

    std::shared_ptr<FrameStatus> frame_status = it->second;
    if (!frame_status->evictable_) {
        LogFatalStream(__FILE__, __LINE__) << "Remove called on non-evictable frame_id " << frame_id;
        return;
    }

    if (frame_status->arc_status_ == ArcStatus::MRU) {
        auto lit = std::find(mru_.begin(), mru_.end(), frame_id);
        if (lit == mru_.end()) {
            LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " marked MRU in alive_map_ but not found in mru_";
            return;
        }
        mru_.erase(lit);
        mru_size_--;
    } else if (frame_status->arc_status_ == ArcStatus::MFU) {
        auto lit = std::find(mfu_.begin(), mfu_.end(), frame_id);
        if (lit == mfu_.end()) {
            LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " marked MFU in alive_map_ but not found in mfu_";
            return;
        }
        mfu_.erase(lit);
        mfu_size_--;
    } else {
        LogFatalStream(__FILE__, __LINE__) << "frame_id " << frame_id << " in alive_map_ has corrupted arc_status_ (neither MRU nor MFU)";
        return;
    }

    alive_map_.erase(it);
    curr_size_--;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t { return curr_size_; }