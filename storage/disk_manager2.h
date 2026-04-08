#pragma once
#include "common/types.h"
#include <string>
#include <unordered_set>

class DiskManager{
	/*
	 * Opens (or creates) the database file at file_path.
     * On creation, initialises the file with a header page at offset 0.
     * On open, reads next_page_id_ from the header so allocation resumes
     * where it left off. Throws std::runtime_error if the file cannot be
     * opened or if the header is corrupt.
	 */
	explicit DiskManager(const std::string& file_path);
	
}
