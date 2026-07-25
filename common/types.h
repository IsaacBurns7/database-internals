#ifndef COMMON_TYPES
#define COMMON_TYPES

#include <cstdint>
#include <limits>
using page_id_t = uint32_t;
using lsn_t = uint64_t;
using slot_id_t = uint16_t;
using frame_id_t = uint32_t;

static constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
static constexpr lsn_t INVALID_LSN = std::numeric_limits<lsn_t>::max();
//invalid slot not used... sentinel is offset of 0 
static constexpr frame_id_t INVALID_FRAME_ID = std::numeric_limits<frame_id_t>::max();

#endif 
