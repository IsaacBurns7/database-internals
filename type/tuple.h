

/* for varchar 
 *	[size, data[MAX_VARCHAR_LEN-sizeof(page_id_t)-sizeof(overflow_offset_t)], page_id of overflow page, offset of overflow page]
 */

// Record layout: oh my god i forgot this... SHOOT
	// [null bitmap: ceil(n/8) bytes] 
	// [fixed section: one slot per field, fixed width per type]
	//   - NUMERICTYPE:   1/2/4/8 bytes  (or 0 if null)
	//   - FLOATTYPE:     4/8 bytes (or 0 if null)
	//   - VARCHAR: 	  2-byte length into variable tail 
	// [variable tail: varchar data appended in index order]
//tuple needs to serialize according to a schema - schema only exists in memory 
    //decides column order, offsets, null bitmap, and inline vs overflow encoding
//tupleserializer converts tuple <-> bytes 
//pagebuilder converts bytes <-> disk storage (USES disk manager) 

#include <vector>
#include "value.h"

//tuple owns the data, schema operates on it
/*
hold a record's bytes
access fields by column
serialize/deserialize according to a schema
pass rows around without exposing storage layout everywhere

tuple owns raw byte buffer
schema is a non-owning reference (this is the type class)
*/
struct Tuple{ //tuple + tupleserializer
    //pointer to schema 
    //raw bits that it owns (make_varchar_nonowning

    std::vector<Value> data;
        //Value overhead: TypeId, width, union? 
    //constructors
        //schema pointer + raw bits
    //destructor
        //use schema pointer to deallocate (you need to know how long the tail is)
    //SerializedSize
    //Serialize
    //Deserialize
    //Destructor
};

struct PageBuilder{
    //pointer to schema
    //create tuples 
    //use diskmanager to
        //allocatepage, 
        //write to the page,
        //in a loop until done 
};

// My take, for later comparison:
// - Tuple should probably be a thin view over row bytes, not a container of Value objects.
// - Schema should be a non-owning descriptor passed to serialization/deserialization.
// - If Tuple owns anything, it should own a single contiguous buffer, because that matches disk/page layout.
// - VARCHAR ownership should not live inside Tuple as per-field heap objects unless you want copy/move complexity now.
// - A separate logical-row class could exist later if you want an ergonomic in-memory API distinct from storage format.
// - PageBuilder feels like storage-layer code; if it survives, it should likely operate on serialized tuples rather than on Values.

// Possible Tuple API A: storage-first, byte-oriented
// struct Tuple {
//     explicit Tuple(const Schema *schema);
//     Tuple(const Schema *schema, std::vector<uint8_t> bytes);
//     auto GetSchema() const -> const Schema *;
//     auto GetBytes() const -> const std::vector<uint8_t> &;
//     auto GetField(uint32_t col_idx) const -> Value;
//     auto SetField(uint32_t col_idx, const Value &v) -> void;
//     auto SerializedSize() const -> uint32_t;
//     auto Serialize(uint8_t *buf) const -> void;
//     static auto Deserialize(const Schema *schema, const uint8_t *buf) -> Tuple;
// };
// This version matches on-page layout best, but it pushes all field access through schema-aware decoding.

// Possible Tuple API B: logical-row-first, value-oriented
// struct Tuple {
//     explicit Tuple(const Schema *schema, std::vector<Value> values);
//     auto GetSchema() const -> const Schema *;
//     auto GetValues() const -> const std::vector<Value> &;
//     auto GetField(uint32_t col_idx) const -> const Value &;
//     auto SetField(uint32_t col_idx, Value v) -> void;
//     auto Serialize(uint8_t *buf) const -> void;
//     static auto Deserialize(const Schema *schema, const uint8_t *buf) -> Tuple;
// };
// This version is easier to use in query code, but VARCHAR ownership and copying become immediate design issues.