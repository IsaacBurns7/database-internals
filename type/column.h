


#pragma once

#include <string>

#include "type.h"  // assumed to define TypeId enum (e.g. INTEGER, BOOLEAN, VARCHAR, etc.)

class Column {
 public:
  /**
   * Construct a fixed-length column (e.g. INTEGER, BOOLEAN, FLOAT).
   * Length is derived from the TypeId.
   * // UNCERTAIN: you may want to derive length automatically from TypeId
   * // rather than requiring the caller to pass it in explicitly.
   */
  Column(std::string name, TypeId type_id, uint32_t length, uint32_t offset);

  /**
   * Construct a VARCHAR column.
   * Length is set to 0 (or some sentinel) since it is variable at runtime.
   * Offset contribution is also resolved at runtime.
   * // UNCERTAIN: depending on your design, you may want a separate
   * // factory/static constructor for varchar vs fixed-length columns
   * // to make the distinction explicit at the call site.
   */
  Column(std::string name, TypeId type_id, uint32_t offset);

  // -----------------------------------------------------------------------
  // Accessors
  // -----------------------------------------------------------------------

  /** Returns the column's name. */
  auto GetName() const -> const std::string &;

  /** Returns the TypeId of this column (e.g. INTEGER, VARCHAR). */
  auto GetType() const -> TypeId;

  /**
   * Returns the fixed storage length of this column in bytes.
   * For inlined columns: the actual byte size (e.g. 4 for INTEGER).
   * For VARCHAR columns: sizeof(uint16_t), i.e. the length prefix only.
   * The variable-length data portion is NOT included here.
   */
  auto GetLength() const -> uint32_t;

  /**
   * Returns the byte offset of this column within a tuple's fixed region.
   * For inlined columns: the actual offset into the tuple.
   * For VARCHAR columns: offset of the uint16_t length prefix.
   * // UNCERTAIN: varchar columns were described as having offset 0 in your
   * // design — clarify whether offset here means "offset of the length
   * // prefix" or whether offset is unused/meaningless for varchar columns.
   */
  auto GetOffset() const -> uint32_t;

  /** Returns true if the column's value is stored inline in the tuple. */
  auto IsInlined() const -> bool;

  /** Returns a human-readable string describing this column. */
  auto ToString() const -> std::string;

 private:
  std::string name_;
  TypeId type_id_;

  /**
   * Byte length of the fixed portion of this column.
   * VARCHAR columns store sizeof(uint16_t) here; variable data is elsewhere.
   */
  uint32_t length_{0};

  /**
   * Byte offset of this column in the tuple's fixed region.
   * // UNCERTAIN: for VARCHAR columns, see note on GetOffset() above.
   */
  uint32_t offset_{0};
};
