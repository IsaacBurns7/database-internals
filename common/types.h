#ifndef COMMON_TYPES
#define COMMON_TYPES

#include <cstdint>
#include <limits>
using page_id_t = uint32_t;
using lsn_t = uint64_t;
using slot_id_t = uint16_t;
using frame_id_t = uint32_t;
using txid_t = uint64_t;
using table_id_t = uint16_t; 
using index_id_t = uint16_t;

static constexpr frame_id_t INVALID_FRAME_ID = std::numeric_limits<frame_id_t>::max();
static constexpr page_id_t  INVALID_PAGE_ID  = std::numeric_limits<page_id_t>::max();
static constexpr table_id_t INVALID_TABLE_ID = std::numeric_limits<table_id_t>::max();
static constexpr index_id_t INVALID_INDEX_ID = std::numeric_limits<index_id_t>::max();
static constexpr slot_id_t  INVALID_SLOT     = std::numeric_limits<slot_id_t>::max();
static constexpr lsn_t      INVALID_LSN      = std::numeric_limits<lsn_t>::max();  // drop if you already have one

#endif 
