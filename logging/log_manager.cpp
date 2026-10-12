#include "log_manager.h"
#include "utils/crc32c.h"
#include <cstring>
#include <mutex>
#include <span>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <system_error>

//at startup, find end of valid log by scanning until a record fails its checksum, truncating torn tail 
LogManager::LogManager(std::string absolute_log_file_path){
}
//append records to in-memory log buffer, assign monotonically increasing LSNs
lsn_t LogManager::append(LogRecordHeader &lrh, std::span<const std::byte> before,
		std::span<const std::byte> after){
	lrh.before_len = before.size(); 
	lrh.after_len = after.size(); 
	lrh.size = sizeof(lrh) + before.size() + after.size(); 
	lrh.crc = 0; //must be 0 while computing so the reader can reproduce it
	std::memset(lrh.reserved, 0, sizeof(lrh.reserved));

	std::scoped_lock<std::mutex> lck(mtx); 
	if(append_buffer_fill_offset + lrh.size > APPEND_BUFFER_SIZE){ 
		return INVALID_LSN; //you gotta flush first buddy 
	}
	lrh.lsn = currentLSN++; 

	std::byte* rec = append_buffer + append_buffer_fill_offset; 
	std::memcpy(rec, &lrh, sizeof(lrh)); 
	if(!before.empty()) std::memcpy(rec + sizeof(lrh), before.data(), before.size());
	if(!after.empty()) std::memcpy(rec + sizeof(lrh) + before.size(), after.data(), after.size()); 

	//CRC over the whole record as laid out in the buffer, then patch all 4 bytes into the crc field
	uint32_t crc = crc32c::compute(rec, lrh.size); 
	std::memcpy(rec + offsetof(LogRecordHeader, crc), &crc, sizeof(crc)); 

	append_buffer_fill_offset += lrh.size; 
	return lrh.lsn; 
}

lsn_t LogManager::log_update(txid_t tx_id, LogOp op, page_id_t page_id, 
		table_id_t table_id, uint16_t before_len, uint16_t after_len, lsn_t prev_lsn,
		std::byte* before_img, std::byte* after_img, index_id_t index_id, slot_id_t slot_id){
	LogRecordHeader lrh{}; 
	lrh.type = LogType::UPDATE;
	lrh.op = op;
	lrh.tx_id = tx_id; 
	lrh.prev_lsn = prev_lsn; 
	lrh.undo_next_lsn = INVALID_LSN; //CLR only
	lrh.page_id = page_id; 
	lrh.table_id = table_id; 
	lrh.index_id = index_id; 
	lrh.slot = slot_id;
	lrh.flags = 0; 
	return append(lrh, {before_img, before_len}, {after_img, after_len}); 
}

//BEGIN / COMMIT / ABORT / END are header-only and only differ by type + prev_lsn
lsn_t LogManager::log_txn_record(txid_t tx_id, LogType type, lsn_t prev_lsn){
	LogRecordHeader lrh{}; 
	lrh.type = type; 
	lrh.op = LogOp::NONE; 
	lrh.tx_id = tx_id; 
	lrh.prev_lsn = prev_lsn; 
	lrh.undo_next_lsn = INVALID_LSN; 
	lrh.flags = 0; 
	// Not meaningful for txn records. Explicit INVALID_* sentinels so recovery can't confuse 
	// this with a real target.
	lrh.page_id  = INVALID_PAGE_ID;
	lrh.table_id = INVALID_TABLE_ID;
	lrh.index_id = INVALID_INDEX_ID;
	lrh.slot     = INVALID_SLOT;
	return append(lrh); 
}

lsn_t LogManager::log_txn_begin(txid_t tx_id){
	// BEGIN is the head of the txn's undo chain, so nothing precedes it.
	return log_txn_record(tx_id, LogType::TXN_BEGIN, INVALID_LSN); 
}

lsn_t LogManager::log_txn_commit(txid_t tx_id, lsn_t prev_lsn){
	return log_txn_record(tx_id, LogType::TXN_COMMIT, prev_lsn); 
}
//txn end is omitted when commit(all updates) or abort(all CLRs) is persisted to secondary storage
lsn_t LogManager::log_txn_abort(txid_t tx_id, lsn_t prev_lsn) { 
	return log_txn_record(tx_id, LogType::TXN_ABORT, prev_lsn); 
}
lsn_t LogManager::log_txn_end(txid_t tx_id, lsn_t prev_lsn){
	return log_txn_record(tx_id, LogType::TXN_END, prev_lsn); 
}

lsn_t LogManager::log_checkpoint_begin() {
	//init lrh header 
	LogRecordHeader lrh{};
	lrh.tx_id = NULL; 
	lrh.type = LogType::CHECKPT_BEGIN; 
	lrh.crc = 0; //must be 0 while computing so the reader can reproduce it
	std::memset(lrh.reserved, 0, sizeof(lrh.reserved));
	
	return append(lrh, {}, {}); 
}

/* CHECKPT_END payload (follows LogRecordHeader; before_len = after_len = 0, length lives in lrh.size)
 *
 * ┌───────────────┬───────────────┬──────────────────────────┬──────────────────────────┐
 * │ att_count u32 │ dpt_count u32 │ AttEntry[att_count] (24B)│ DptEntry[dpt_count] (16B)│
 * └───────────────┴───────────────┴──────────────────────────┴──────────────────────────┘
 *
 * AttEntry: tx_id u64 | last_lsn u64 | status u8 | reserved[7] (zeroed)
 * DptEntry: page_id u32 | reserved u32 (zeroed) | rec_lsn u64
 *
 * lrh.size == sizeof(LogRecordHeader) + 8 + 24*att_count + 16*dpt_count  (reader validates this)
 * Entries may be misaligned in the read buffer => read with memcpy, not reinterpret_cast.
 * Entries are sorted by key (tx_id / page_id) so output is reproducible.
 */
//CHECKPT_END payload entries (format documented above log_checkpoint_end in log_manager.cpp)
lsn_t LogManager::log_checkpoint_end(std::unordered_map<txid_t, lsn_t> ATT,
	std::unordered_map<page_id_t, lsn_t> DPT) {
	//init lrh header 
	LogRecordHeader lrh{};
	lrh.tx_id = NULL; 
	lrh.type = LogType::CHECKPT_END; 
	lrh.crc = 0; //must be 0 while computing so the reader can reproduce it
	std::memset(lrh.reserved, 0, sizeof(lrh.reserved));
 	
	std::vector<std::byte> before_img_ATT(sizeof(AttEntry) * ATT.size());
	std::vector<std::byte> after_img_DPT(sizeof(DptEntry) * DPT.size());
	for(const auto& [txid, lastLSN] : ATT){ 
		uint8_t status = 0; //default runnning => where to get this from...? maybe I have to modify ATT 
		AttEntry entry{txid, lastLSN, status, }; 
		std::memset(entry.reserved, 0, sizeof(entry.reserved));
		std::span<const std::byte> bytes = std::as_bytes(std::span{&entry, 1}); 
		before_img_ATT.insert(before_img_ATT.end(), bytes.begin(), bytes.end());
	}
	for(const auto& [pageid, recLSN] :  DPT){ 
		DptEntry entry{pageid, 0, recLSN}; 
		std::span<const std::byte> bytes = std::as_bytes(std::span{&entry, 1}); 
		after_img_DPT.insert(after_img_DPT.end(), bytes.begin(), bytes.end()); 
	}
	//before is ATT, after is DPT 
	return append(lrh, before_img_ATT, after_img_DPT); 
}

//make log durable up to requested LSN, signal completion after fsync returns;
//batches concurrent requests
	//committers waiting at the same time can share an fsync
		//first waiter is leader, write everything in buffer so far
		//all other waiters have a condition variable for their requested target (get_flushed_LSN)
		//when leader is done, notify_all() => if target achieved, return, otherwise try to become leader / fall asleep if u fail

//helpers for durable append 
namespace {

struct Fd {
    int fd = -1;
    explicit Fd(int f) : fd(f) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

//this later can be a std::abort() if we end up catching errors... 
[[noreturn]] void fail(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

// meta: true = also flush metadata (used for directories).
int sync_fd(int fd, bool meta) {
#ifdef MAC
    (void)meta;
    // F_FULLFSYNC flushes the drive's write cache. Some filesystems
    // (network, FAT) don't support it, so fall back to fsync.
    if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
    if (errno == ENOTSUP || errno == EINVAL) return ::fsync(fd);
    return -1;
#else
    //return meta ? ::fsync(fd) : ::fdatasync(fd);
	return ::fsync(fd); 
#endif
}

} // namespace

constexpr uint32_t INVALID_LOG_OFFSET = UINT32_MAX; 
void LogManager::flush_to(lsn_t target) {
	if(target > currentLSN){
		printf("Requested a flush to %llu, currentLSN = %llu", target, currentLSN);  
	}
//for each thread, wait on condition_variable cv (get_flushed_LSN > target) 
//become leader? 
//acquire mutex 
	//find offset in append buffer of target lsn, copy to fill buffer 
	//give up mutex
	//fsync fill buffer 
		//if it fails, panic 
	//notify_all(cv)
	auto find_offset = [&]() -> uint32_t{
		std::scoped_lock<std::mutex> lck(mtx); 
		lsn_t lsn_found = 0; 
		uint32_t offset = 0; 
		while(lsn_found != target && 
				offset < sizeof(append_buffer) + sizeof(LogRecordHeader)){
			LogRecordHeader *lrh = (LogRecordHeader*)(append_buffer + offset); 
#ifdef DEBUG 
			printf("At offset %d, found lsn %llu\n", offset, lsn_found); 
#endif 
			lsn_found = lrh->lsn; 
			offset += lrh->size; 
		}
		if(offset >= sizeof(append_buffer) + sizeof(LogRecordHeader)){
			printf("Offset at end of append buffer, could not find target lsn: %llu\n", target);  
			return INVALID_LOG_OFFSET; 
		}
		LogRecordHeader *target_lrh = (LogRecordHeader*)(append_buffer + offset); 
		offset += target_lrh->size; //offset now includes log w/ target lsn 
		std::memcpy(fill_buffer, append_buffer, offset); 
		return offset; 
	};


	auto append_durable = [](const std::filesystem::path& path,
                    std::span<const std::byte> buf, uint32_t offset) -> void {
		int raw = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_EXCL | O_CLOEXEC, 0644); 
		bool created = raw >= 0; 
		if(!created){
			if(errno != EEXIST) fail("open"); 
			raw = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC); 
			if(raw < 0) fail("open"); 
		}
		Fd fd(raw);
		const std::byte* p = buf.data(); 
		size_t left = offset; 
		while(left > 0){
			ssize_t n = ::write(fd.fd, p, left); 
			if(n < 0){
				if(errno == EINTR) continue; 
				fail("write"); 
			}
			p += n; 
			left -= static_cast<std::size_t>(n);
		}

		//DO NOT RETRY ON FAILURE! FAILURE IS FATAL! 
		if(sync_fd(fd.fd, false) != 0) fail("fsync"); 

		if(created){
			auto parent = path.parent_path(); 
			if(parent.empty()) parent = "."; 
			Fd dir(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)); 
			if(dir.fd < 0) fail("open dir"); 
			if(sync_fd(dir.fd, true != 0)) fail("fsync dir"); 
		}
	};
}
