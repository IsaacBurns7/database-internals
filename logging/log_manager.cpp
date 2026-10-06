#include "log_manager.h"
#include "utils/crc32c.h"
#include <mutex>

//at startup, find end of valid log by scanning until a record fails its checksum, truncating torn tail 
LogManager::LogManager(std::string absolute_log_file_path){
}
//append records to in-memory log buffer, assign monotonically increasing LSNs
lsn_t LogManager::log_update(txid_t tx_id, LogOp op, page_id_t page_id, 
		table_id_t table_id, uint16_t before_len, uint16_t after_len, lsn_t prev_lsn,
		std::byte* before_img, std::byte* after_img, index_id_t index_id, slot_id_t slot_id){
	LogRecordHeader lrh{}; 
	lrh.tx_id = tx_id; 
	lrh.op = op;
	lrh.page_id = page_id; 
	lrh.before_len = before_len; 
	lrh.after_len = after_len; 
	lrh.flags = 0; 
	lrh.type = LogType::UPDATE;
	lrh.index_id = index_id; 
	lrh.table_id = table_id; 
	lrh.slot = slot_id;
	std::memset(lrh.reserved, 0, sizeof(lrh.reserved));
	lrh.size = sizeof(lrh) + before_len + after_len; //is this right... ?  
	lsn_t ret; 
	{
		//check capacity and swap buffers if needed? clanker might be clanking... 
		if(append_buffer_fill_offset + sizeof(lrh) + before_len + after_len){ 
			return INVALID_LSN; //you gotta flush first buddy 
		}
		lrh.prev_lsn = prev_lsn; 
		std::scoped_lock<std::mutex> lck(mtx); 
		lrh.lsn = currentLSN++; //compiler wont reorder this right? 
		ret = lrh.lsn; 
		uint16_t offset_advancement = 0;
		std::memcpy(append_buffer + append_buffer_fill_offset, (void*)&lrh, sizeof(lrh)); 
		offset_advancement += sizeof(lrh);
		std::memcpy(append_buffer + append_buffer_fill_offset + offset_advancement, before_img, before_len);
		offset_advancement += before_len;
		std::memcpy(append_buffer + append_buffer_fill_offset + offset_advancement, after_img, after_len); 
		offset_advancement += after_len; 
		*(append_buffer + append_buffer_fill_offset + offsetof(LogRecordHeader, crc)) = 
			(std::byte)crc32c::compute((const std::byte*)(append_buffer + append_buffer_fill_offset), offset_advancement); 
		append_buffer_fill_offset += offset_advancement; 
	}
	return ret; 
}

lsn_t LogManager::log_txn_begin(txid_t tx_id){
	LogRecordHeader lrh{};          // value-init zeroes everything, incl. reserved
	lrh.tx_id   = tx_id;
	lrh.type    = LogType::TXN_BEGIN;
	lrh.op      = LogOp::NONE;
	lrh.flags   = 0;
	lrh.before_len = 0;
	lrh.after_len  = 0;
	lrh.size    = sizeof(lrh);      // header only, no images

	// BEGIN is the head of the txn's undo chain, so nothing precedes it.
	// Assumes you have a sentinel; if 0 is a valid LSN in your scheme, don't rely on {} here.
	lrh.prev_lsn = INVALID_LSN;

	// Not meaningful for BEGIN. If 0 is a valid page/table/index id, set explicit
	// INVALID_* sentinels so recovery can't confuse this with a real target.
	lrh.page_id  = INVALID_PAGE_ID;
	lrh.table_id = INVALID_TABLE_ID;
	lrh.index_id = INVALID_INDEX_ID;
	lrh.slot     = INVALID_SLOT;

	lsn_t ret;
	{
		std::scoped_lock<std::mutex> lck(mtx);
		if(append_buffer_fill_offset + sizeof(lrh)){ 
			return INVALID_LSN; //you gotta flush first buddy 
		}

		lrh.lsn = currentLSN++;
		ret = lrh.lsn;

		// No payload, so CRC the local header before copying.
		// crc field must be 0 while computing so the reader can reproduce it.
		lrh.crc = 0;
		lrh.crc = crc32c::compute(reinterpret_cast<const std::byte*>(&lrh), sizeof(lrh));

		std::memcpy(append_buffer + append_buffer_fill_offset, &lrh, sizeof(lrh));
		append_buffer_fill_offset += sizeof(lrh);
	}
	return ret;
}
lsn_t LogManager::log_txn_commit(txid_t tx_id){
	
}
//txn end is omitted when commit(all updates) or abort(all CLRs) is persisted to secondary storage
void LogManager::log_txn_abort(txid_t tx_id, BufferPoolManager &bpm) {
	//how to abort...
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
