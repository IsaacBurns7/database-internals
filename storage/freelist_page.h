#pragma once

#include "common/config.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <cassert>

// PENDING (VPID/PPID directory, see storage/disk_manager.h design notes):
// unlike SlottedPageHeader, this struct deliberately does NOT get a VPID/
// generation field. The directory rebuild-on-startup scan needs to tell
// "free PPID" apart from "live PPID with a VPID bound to it" — it does that
// by walking THIS chain first (rooted at global_metadata_.freelist_head,
// already persisted) to collect the full set of free PPIDs, then treating
// every PPID not in that set as live. So a free page's own bytes never need
// to self-describe; membership in this chain already is the free/live signal.
struct Freelist_Page{
	uint32_t next_freelist_page; //next page in the chain
	page_id_t current_id_count; 

	static constexpr size_t FIXED_SIZE = sizeof(uint32_t) + sizeof(page_id_t);
	static constexpr size_t MAX_FREE_IDS = (PAGE_SIZE - FIXED_SIZE) / sizeof(page_id_t);
	static constexpr size_t USED_SIZE = FIXED_SIZE + MAX_FREE_IDS * sizeof(page_id_t);

	page_id_t free_page_ids[MAX_FREE_IDS];
} __attribute__((packed));

static_assert(Freelist_Page::USED_SIZE <= PAGE_SIZE, "Freelist page layout exceeds PAGE_SIZE");
static_assert(sizeof(Freelist_Page) == Freelist_Page::USED_SIZE, "Freelist page size mismatch");
