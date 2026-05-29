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
	return columns_[col_idx];
}
auto Schema::GetColIdx(const std::string &col_name) const -> std::optional<uint32_t> { 
	for(size_t i = 0;i < columns_.size();i++){
		const Column &col = columns_[i];
		if(col.GetName() == col_name){
			return static_cast<uint32_t>(i);
		}
	}
	return std::nullopt; 
}
auto Schema::GetColumns() const -> const std::vector<Column> & { return columns_; }
auto Schema::GetColumnCount() const -> uint32_t { return static_cast<uint32_t>(columns_.size()); }

  // -----------------------------------------------------------------------
  // Size / Layout
  // -----------------------------------------------------------------------
auto Schema::GetFixedSize() const -> uint32_t { //varchar columns contribute sizeof(len_prefix), currently equals sizeof(uint16_t)
	uint32_t ret = 0;
	for(const auto& col: columns_){
		ret += col.length_;
		if(col.type_id_ == TypeID::VARCHAR) ret += sizeof(uint16_t); //length prefix is a uint16_t
	}
	return ret; 
}
// std::vector<Column> columns_;
	// std::vector<uint32_t> fixed_size_columns;
	// uint32_t fixed_size_{0};


auto Schema::HasVariableLengthColumns() const -> bool { return fixed_size_columns.size() != columns_.size(); }
auto Schema::IsFixedLength(uint32_t col_idx) const -> bool {return !columns_[col_idx].IsVariableLength(); }
//logical/in-memory record size, NOT disk size
auto Schema::RecordSize(const Tuple& record) const -> uint32_t{
	uint32_t ret = 0;
	for(uint32_t i = 0;i < columns_.size(); i++){
		Value curr = record.get(i); 
		ret += curr.width;
		if(curr.type_id_ == TypeID::VARCHAR){
			ret += sizeof(curr.val.varchar.len);
			ret += curr.val.varchar.len;
		}
	}
}
// -----------------------------------------------------------------------
// Debug / Serialization
// -----------------------------------------------------------------------
auto Schema::ToString() const -> std::string { //maybe another one for ostream 
	//maybe use same format as serialize
	std::string ret{"Size: " + columns_.size() + "\n"}; 
	for(uint32_t i = 0; i < columns_.size(); i++){
		const &Column col = columns_[i];
		ret += "Column " + i + ": " + col.ToString(); 
	}
	return ret; 
}
/* format:
 * [uint32_t num_cols] 
 * repeat num_cols times: 
 * 		[serialized_col] mayhaps [uint8_t TypeID] [uint32_t fixed_size]  
 */
auto SerializeSchema(uint8_t *buf) const -> uint32_t { //serializes this schema
	
}
static auto Deserialize(const uint8_t *buf) -> std::unique_ptr<Schema> {
	//read num_cols 
	//for i in range [0, num_cols]: 
	//	Deserialize col using 
	//		static auto Deserialize(const uint8_t *buf) -> std::unique_ptr<Column>; 
}
