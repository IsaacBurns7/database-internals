#pragma once 

//schema 
//what does schema need to do ? 
//well schema's purpose to me, is to take some give an interface by which to read and write records into the database 
	//Schema.write(page_id_t node, uint16_t record_id, uint8_t* record);
	//Schema.read(page_id_t node, uint16_t record_id) -> uint8_t*;
//schema is {a list of columns, length} 
	//currently no support for non inlined columns (varchar is len+data instead of overflow_page_id+offset+len) 
		//would probably want list of non inlined columns if this was the case 
	//column is typeid, name, length, offset 
//what should it be able to do .... 















#pragma once

#include <string>
#include <vector>

#include "column.h"  // assumed to define Column, TypeId, etc.

class Schema {
 public:
  /** Construct a schema from an ordered list of columns. */
  explicit Schema(const std::vector<Column> &columns);

  /**
   * Construct a schema that is a subset/projection of another schema.
   * col_indices specifies which columns to include, in the given order.
   * Useful for projection operators in query execution.
   */
  Schema(const Schema &other, const std::vector<uint32_t> &col_indices);

  // -----------------------------------------------------------------------
  // Lookup / Access
  // -----------------------------------------------------------------------

  /** Returns the column at the given index. */
  auto GetColumn(uint32_t col_idx) const -> const Column &;

  /**
   * Returns the index of the column with the given name.
   * // UNCERTAIN: should this throw, return -1, or return std::optional?
   * // Depends on whether callers always have validated names beforehand.
   */
  auto GetColIdx(const std::string &col_name) const -> uint32_t;

  /** Returns all columns in order. */
  auto GetColumns() const -> const std::vector<Column> &;

  /** Returns the number of columns. */
  auto GetColumnCount() const -> uint32_t;

  // -----------------------------------------------------------------------
  // Size / Layout
  // -----------------------------------------------------------------------

  /**
   * Returns the fixed portion of a tuple's size in bytes.
   * For inlined columns: sum of their lengths.
   * For varchar columns: sizeof(uint16_t) per column (the length prefix).
   * The actual varchar data is accounted for separately at runtime.
   */
  auto GetFixedSize() const -> uint32_t;

  /** Returns true if the schema contains at least one VARCHAR column. */
  auto HasVariableLengthColumns() const -> bool;

  /**
   * Returns the indices of all non-inlined (VARCHAR) columns.
   * Useful for iterating only over columns that need special handling.
   */
  auto GetUninlinedColumns() const -> const std::vector<uint32_t> &;

  // -----------------------------------------------------------------------
  // Runtime offset resolution
  // -----------------------------------------------------------------------

  /**
   * Given a raw tuple pointer, returns the byte offset at which column
   * col_idx begins within that tuple.
   *
   * For inlined columns this is just Column::GetOffset().
   * For varchar columns this requires walking the tuple at runtime because
   * prior varchar columns have variable lengths.
   *
   * // UNCERTAIN: you may prefer to put this on Tuple rather than Schema,
   * // since it needs access to the raw tuple bytes. Included here as a
   * // candidate; remove if it feels like the wrong layer.
   */
  auto GetColumnOffset(uint32_t col_idx, const char *tuple_data) const -> uint32_t;

  // -----------------------------------------------------------------------
  // Debug / Serialization
  // -----------------------------------------------------------------------

  /** Returns a human-readable string describing the schema. */
  auto ToString() const -> std::string;

  /**
   * Serialize the schema to a byte buffer (for catalog persistence).
   * // UNCERTAIN: signature depends on your serialization strategy
   * // (e.g. WriteBuffer, std::ostream, or raw byte vector).
   */
  // void SerializeTo(WriteBuffer &out) const;
  // static auto DeserializeFrom(ReadBuffer &in) -> Schema;

 private:
  /** Ordered list of columns. */
  std::vector<Column> columns_;

  /**
   * Indices of non-inlined (VARCHAR) columns within columns_.
   * Pre-computed at construction for fast iteration.
   */
  std::vector<uint32_t> uninlined_columns_;

  /**
   * The fixed-size footprint of a tuple under this schema.
   * Pre-computed at construction.
   */
  uint32_t fixed_size_{0};
};
