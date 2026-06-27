#pragma once 

#include "common/config.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <cassert>

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
