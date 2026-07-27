#pragma once 

enum TypeId {
	INVALID = 0,
	BOOLEAN, //bool  
	TINYINT, //int8_t
	SMALLINT, //int16_t
	INTEGER, //int32_t 
	BIGINT, //int64_t
	DECIMAL, //double
	VARCHAR //std string or char* 
	// TIMESTAMP,
};

enum class CmpBool { CmpFalse = 0, CmpTrue = 1, CmpNull = 2 };

class Type{
	explicit Type(TypeId type_id): type_id_(type_id) {} 
	virtual ~Type() = default;
	inline static auto GetInstance(TypeId type_id) -> Type* { return k_types[type_id]; }
	inline auto GetTypeId() const -> TypeId { return type_id_; }

		// Serialize this value into the given storage space.
	virtual void SerializeTo(const Value &val, char *storage) const;

	  // Deserialize a value of the given type from the given storage space.
	virtual auto DeserializeFrom(const char *storage) const -> Value;
	
	virtual auto CompareEquals(const Value &left, const Value &right) const -> CmpBool;
	virtual auto CompareNotEquals(const Value &left, const Value &right) const -> CmpBool;
	virtual auto CompareLessThan(const Value &left, const Value &right) const -> CmpBool;
	virtual auto CompareLessThanEquals(const Value &left, const Value &right) const -> CmpBool;
	virtual auto CompareGreaterThan(const Value &left, const Value &right) const -> CmpBool;
	virtual auto CompareGreaterThanEquals(const Value &left, const Value &right) const -> CmpBool;

	// Other mathematical functions
	virtual auto Add(const Value &left, const Value &right) const -> Value;
	virtual auto Subtract(const Value &left, const Value &right) const -> Value;
	virtual auto Multiply(const Value &left, const Value &right) const -> Value;
	virtual auto Divide(const Value &left, const Value &right) const -> Value;
	virtual auto Modulo(const Value &left, const Value &right) const -> Value;
	virtual auto Min(const Value &left, const Value &right) const -> Value;
	virtual auto Max(const Value &left, const Value &right) const -> Value;
	virtual auto Sqrt(const Value &val) const -> Value;
	virtual auto OperateNull(const Value &val, const Value &right) const -> Value;
	virtual auto IsZero(const Value &val) const -> bool;
protected:
	TypeId type_id_;
	static Type *k_types[8];
};
