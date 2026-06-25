//tuple needs to serialize according to a schema - schema only exists in memory 
//decides column order, offsets, null bitmap, and inline vs overflow encoding

/* for varchar 
 *	[size, data[MAX_VARCHAR_LEN-sizeof(page_id_t)-sizeof(overflow_offset_t)], page_id of overflow page, offset of overflow page]
 */

// Record layout:
	// [null bitmap: ceil(n/8) bytes]
	// [fixed section: one slot per field, fixed width per type]
	//   - NUMERICTYPE:   1/2/4/8 bytes  (or 0 if null)
	//   - FLOATTYPE:     4/8 bytes (or 0 if null)
	//   - VARCHAR: 	  2-byte length into variable tail 
	// [variable tail: varchar data appended in index order]

//tupleserializer converts tuple <-> bytes 
//pagebuilder converts bytes <-> disk storage (USES disk manager) 
