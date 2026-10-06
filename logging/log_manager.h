#pragma once 

#include "buffer/buffer_pool_manager.h"
#include "common/types.h"
#include <span>

enum class LogType : uint8_t{
	INVALID = 0,
	UPDATE, 
	CLR,
	TXN_BEGIN,
	TXN_COMMIT,
	TXN_ABORT,
	TXN_END,
	CHECKPT_BEGIN,
	CHECKPT_END
};

enum class LogOp : uint8_t{
	NONE = 0, //for non-update/non-clr records
//key lives in tuple 
	INSERT_SLOT, 	//empty => full tuple
	DELETE_SLOT, 	//full tuple => empty 
	UPDATE_SLOT, 	//old tuple => new tuple 
					//OR changed bytes => changed bytes 
//redo-only => only after-images 
	INIT_PAGE, 		//page_type, level, low_fence_len + low_fence, high_fence_len + high_fence 
						//ADD index_id when secondary indexes are supported 
							//or dont b/c it'd be in LogRecordHeader...
	SET_SIBLING, 	//which: uint8_t (0 => NEXT, 1 => PREV), sibling: page_id_t 
						//for B+ leaf
	SET_CHILD, 		//slot: u16, child: page_id_t 
						//LEFTMOST sentinel for extra pointer before key 0 
	FULL_PAGE 		//after-image = entire page, simplest SMO logging
};

struct LogRecordHeader {
    uint32_t   size;           //  0  total record bytes: header + before + after
    uint32_t   crc;            //  4  CRC32C over whole record, computed with this field = 0
    lsn_t      lsn;            //  8
    lsn_t      prev_lsn;       // 16  txn backchain; INVALID_LSN for first record
    lsn_t      undo_next_lsn;  // 24  CLR only; INVALID_LSN otherwise
    txid_t     tx_id;          // 32
    page_id_t  page_id;        // 40
    table_id_t table_id;       // 44
    index_id_t index_id;       // 46
    uint16_t   slot;           // 48
    uint16_t   flags;          // 50
    uint16_t   before_len;     // 52
    uint16_t   after_len;      // 54
    LogType    type;           // 56
    LogOp      op;             // 57
    uint8_t    reserved[6];    // 58
};                             // 64

static_assert(sizeof(lsn_t) == 8 && sizeof(txid_t) == 8);
static_assert(sizeof(page_id_t) == 4);
static_assert(sizeof(table_id_t) == 2 && sizeof(index_id_t) == 2);
static_assert(sizeof(LogType) == 1 && sizeof(LogOp) == 1);

static_assert(std::is_trivially_copyable_v<LogRecordHeader>);
static_assert(std::is_standard_layout_v<LogRecordHeader>);
static_assert(std::has_unique_object_representations_v<LogRecordHeader>); // no padding → deterministic CRC

static_assert(sizeof(LogRecordHeader)  == 64);
static_assert(alignof(LogRecordHeader) == 8);
static_assert(offsetof(LogRecordHeader, undo_next_lsn) == 24);
static_assert(offsetof(LogRecordHeader, tx_id)         == 32);
static_assert(offsetof(LogRecordHeader, page_id)       == 40);
static_assert(offsetof(LogRecordHeader, type)          == 56);
static_assert(offsetof(LogRecordHeader, reserved)      == 58);;
static_assert(offsetof(LogRecordHeader, lsn) == 8);
static_assert(std::is_trivially_copyable_v<LogRecordHeader>);
static_assert(std::is_standard_layout_v<LogRecordHeader>);

struct LogRecordView{
	LogRecordHeader hdr; 
	std::span<const std::byte> before; //read buffer 
	std::span<const std::byte> after;  
};
//USE
	//LogRecordView v = parse(buf);   // header copied; spans point into buf
	//apply_redo(page, v.hdr.slot, v.after);/

/* Note: Redo is page-orientated, blindly reapply. 
 * Undo is logical, and uses the key from the before-image. The original page_id must hold key_id and pass a fence check, 
 * otherwise the tree must be traversed from root and undo the page where it lives... 
 */

#define APPEND_BUFFER_SIZE (64 << 3) << 20 //(64 MiB) 
#define FILL_BUFFER_SIZE (64 << 3) << 20 //(64 MiB)

class LogManager{
public:
	//at startup, find end of valid log by scanning until a record fails its checksum, truncating torn tail 
	LogManager(std::string absolute_log_file_path); 
	lsn_t inline get_flushed_LSN(){ return flushedLSN; }
	lsn_t inline get_current_LSN(){ return currentLSN; }
	//append records to in-memory log buffer, assign monotonically increasing LSNs
	//void log_update(txid_t tx_id, LogOp op, page_id_t page_id, uint16_t len, uint16_t offset, std::byte* before_img, std::byte* after_img);
	lsn_t log_update(txid_t tx_id, LogOp op, page_id_t page_id, 
		table_id_t table_id, uint16_t before_len, uint16_t after_len, lsn_t prev_lsn,
		std::byte* before_img, std::byte* after_img, index_id_t index_id = 0, slot_id_t slot_id = 0);
		//Update can be any LogOp
	lsn_t log_txn_begin(txid_t tx_id);
	lsn_t log_txn_commit(txid_t tx_id); 
		//txn end is omitted when commit(all updates) or abort(all CLRs) is persisted to secondary storage
	void log_txn_abort(txid_t tx_id, BufferPoolManager &bpm); 
		//how to abort... 
//LATER: CheckpointManager owns Checkpointing (and must know where master record lives)
	void log_checkpoint_begin(); 
	void log_checkpoint_end(std::unordered_map<txid_t, lsn_t> &ATT, 
			std::unordered_map<page_id_t, lsn_t> &DPT);
	//make log durable up to requested LSN, signal completion after fsync returns;
	//batches concurrent requests 
		//committers waiting at the same time can share an fsync 
			//first waiter is leader, write everything in buffer so far
			//all other waiters have a condition variable for their requested target (get_flushed_LSN)
			//when leader is done, notify_all() => if target achieved, return, otherwise become try to become leader / fall asleep if u fail
	void flush_to(lsn_t target); 
	//forward iterator that scans from a specified begin_LSN till end of log
	
	//REMEMBER fsync is failure => ABORT! 
private: 
	std::string absolute_log_file_path; 
		//preallocate the log file in large chunks, fdatasync doesn't also have to persist file-size metadata on every flush, which "roughly doubles the cost" - i don't believe that, but claude does... 
		//rewriting partially filled tail block on the next flush is safe, as the previously durable bytes should be identical 
	lsn_t currentLSN; //only read / written to under mtx 
	std::atomic<lsn_t> flushedLSN; //isnt this also the master record...? or does the master record lag behind this, and is only brought up to speed during checkpoints? 
								   //is read outside the mtx 
	//TransactionManager owns ATT
	//BufferPoolManager owns DPT 
	//RecoveryManager runs ARIES (and must know where master record lives)
	uint64_t append_buffer_base_offset; //where we start writing to the file 
	std::atomic<uint64_t> append_buffer_fill_offset; //=> current_len 
	std::atomic<uint64_t> durable_end_offset; //if not flushing, equal to append_buffer_base_offset 
											  //atomic b/c it must be accessed by BPM's eviction check 
	bool currently_flushing; 
	std::mutex mtx; 
		//for both reserving space and copying bytes into the append_buffer 
		//+ held by flush_to when copying append_buffer to fill_buffer
	std::byte append_buffer[APPEND_BUFFER_SIZE]; 
	std::byte fill_buffer[FILL_BUFFER_SIZE];

	//later concurrency addition
		//each writer gets a range to copy into, and copies it's log records into that range 
		//once it's done copying, there may be other writers in its prefix. 
		//it must wait on those prefix writers, until the high water-mark is at or above the end of its range 
};









/* ANOTHER IDEA FOR LogRecord structs... ? 
struct LogRecord_UPDATE{
};
struct LogRecord_CLR{
};
struct LogRecord_TXN_BEGIN{
};
struct LogRecord_TXN_COMMIT{
};
struct LogRecord_TXN_ABORT{
};
struct LogRecord_TXN_END{
};
struct LogRecord_CHECKPT_BEGIN{
};
struct LogRecord_CHECKPT_END{
};
*/
