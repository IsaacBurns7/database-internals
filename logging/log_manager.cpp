#include "log_manager.h"
#include "utils/crc32c.h"
#include <cstring>
#include <mutex>
#include <span>

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

void LogManager::log_checkpoint_begin() {

}

void LogManager::log_checkpoint_end(std::unordered_map<txid_t, lsn_t> &ATT,
	std::unordered_map<page_id_t, lsn_t> &DPT) {
}

//make log durable up to requested LSN, signal completion after fsync returns;
//batches concurrent requests
	//committers waiting at the same time can share an fsync
		//first waiter is leader, write everything in buffer so far
		//all other waiters have a condition variable for their requested target (get_flushed_LSN)
		//when leader is done, notify_all() => if target achieved, return, otherwise try to become leader / fall asleep if u fail
void LogManager::flush_to(lsn_t target) {
}
