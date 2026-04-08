#ifndef COMMON_TYPES
#define COMMON_TYPES

#include <cstdint>
#include <limits>
using page_id_t = uint32_t;
using lsn_t = uint64_t;
static constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
static constexpr lsn_t INVALID_LSN = std::numeric_limits<lsn_t>::max();

#endif 
