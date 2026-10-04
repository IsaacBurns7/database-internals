#pragma once 

#include "common/types.h"
#include <span>

enum LogType : uint8_t{
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

enum LogOp : uint8_t{
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


struct LogRecordHeader{
	uint32_t size;
	uint32_t crc; //checksum 
	lsn_t lsn; 
	lsn_t prev_lsn;
	txid_t tx_id; 
	LogType type; //uint8_t enum 
	LogOp op; 	  //uint8_t enum
	uint16_t flags; 
	page_id_t page_id; 
	uint16_t slot; 
	uint16_t before_len; 
	uint16_t after_len; 
	uint16_t pad; 
	//table_id_t table_id; 
	//index_id_t index_id; 
		//ADD index_id and table_id when multiple tables + secondary indexes are supported 
};
//serialize field by field.... ? 
static_assert(sizeof(LogRecordHeader) == 48); 

struct LogRecordView{
	LogRecordHeader hdr; 
	lsn_t undo_next_lsn; //CLR only 
	std::span<const std::byte> before; //read buffer 
	std::span<const std::byte> after;  
};

/* Note: Redo is page-orientated, blindly reapply. 
 * Undo is logical, and uses the key from the before-image. The original page_id must hold key_id and pass a fence check, 
 * otherwise the tree must be traversed from root and undo the page where it lives... 
 */

class LogRecordManager{
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
