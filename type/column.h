#pragma once

#include <string>

#include "type.h"  

class Column {
 public:
  /**
   * Construct a fixed-length column 
   * NUMERIC, BOOLEAN, FLOAT
   */
  Column(std::string name, TypeId type_id, uint32_t length, uint32_t offset);
  /**
   * Construct a VARCHAR column.
   * Length is set to 0 since it is variable at runtime.
   * Offset contribution is resolved at runtime.
   * 	should it be stored? hmm... 
   */
  Column(std::string name, TypeId type_id, uint32_t offset);
  
  auto GetName() const -> const std::string &;
  auto GetType() const -> TypeId;
  /**
   * Returns the fixed storage length of this column in bytes.
   * For inlined columns: the actual byte size (e.g. 4 for INTEGER).
   * For VARCHAR columns: sizeof(uint16_t), i.e. the length prefix only.
   */
  auto GetLength() const -> uint32_t;
  /**
   * Returns the byte offset of this column within a tuple's fixed region.
   * For inlined columns: the actual offset into the tuple.
   * For VARCHAR columns: offset of the uint16_t length prefix, after which the data section proceeds... 
   */
  auto GetOffset() const -> uint32_t;
  //the below is meaningless currently 
	  /** Returns true if the column's value is stored inline in the tuple. */
	  // auto IsInlined() const -> bool;
  /** Returns a human-readable string describing this column. */
  auto ToString() const -> std::string;

 private:
  std::string name_;
  TypeId type_id_;
  uint32_t length_{0}; //VARCHAR columns store sizeof(uint16_t) here; variable data is elsewhere.
  uint32_t offset_{0}; //offset in record's fixed region does not consider VARCHAR column's data section
};
