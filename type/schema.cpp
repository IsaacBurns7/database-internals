#include "schema.h"

//psuedocode goal: figure out what schema needs from column and tuple 
  // Record layout:
	// [null bitmap: ceil(n/8) bytes]
	// [fixed section: one slot per field, fixed width per type]
	//   - NUMERICTYPE:   1/2/4/8 bytes  (or 0 if null)
	//   - FLOATTYPE:     4/8 bytes (or 0 if null)
	//   - VARCHAR: 	  2-byte length into variable tail 
	// [variable tail: varchar data appended in index order]

//schema class data 
	// std::vector<Column> columns_;
	// std::vector<uint32_t> fixed_size_columns;
	// uint32_t fixed_size_{0};

explicit Schema::Schema(const std::vector<Column> &columns): columns_(columns){
	for(size_t i = 0;i < columns.size();i++){
		if(!columns[i].IsVariableLength()) fixed_size_columns.push_back(i);
		fixed_size_ += columns[i].GetLength(); 
	}
}
  /**
   * Construct a schema that is a subset/projection of another schema.
   * col_indices specifies which columns to include, in the given order.
   * Useful for projection operators in query execution.
   */
Schema::Schema(const Schema &other, const std::vector<uint32_t> &col_indices){
	columns_.reserve(col_indices.size()); 
	for(const auto& index: col_indices){
		Column &col = other.columns_[index];
		columns_.push_back(col);
		if(!col.IsVariableLength()) fixed_size_columns.push_back(columns_.size()-1);
		fixed_size_ += col.GetLength(); 
	}
}
  // -----------------------------------------------------------------------
  // Lookup / Access
  // -----------------------------------------------------------------------
auto Schema::GetColumn(uint32_t col_idx) const -> const Column & {
	
}
  auto GetColIdx(const std::string &col_name) const -> uint32_t; //returns -1 if column with given col_name doesn't exist 
  auto GetColumns() const -> const std::vector<Column> &;
  auto GetColumnCount() const -> uint32_t;

  // -----------------------------------------------------------------------
  // Size / Layout
  // -----------------------------------------------------------------------
  auto GetFixedSize() const -> uint32_t; //varchar columns contribute sizeof(len_prefix), currently equals sizeof(uint16_t)
  auto HasVariableLengthColumns() const -> bool;
  auto GetVariableLengthColumns() const -> const std::vector<uint32_t> &;
  
  // -----------------------------------------------------------------------
  // Runtime size resolution
  // -----------------------------------------------------------------------
  auto RecordSize(const Tuple& record) const -> uint32_t; 
  // -----------------------------------------------------------------------
  // Debug / Serialization
  // -----------------------------------------------------------------------
  auto ToString() const -> std::string; //maybe another one for ostream 
  auto SerializeSchema(uint8_t *buf) const -> uint32_t; //serializes this schema 
  static auto DeserializeSchema(uint8_t *schema) -> Schema; //deserializes into this schema - should this be a constructor ?
