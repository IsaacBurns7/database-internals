#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "type/types.h"

class Value{ 
	friend class Type;
	friend class IntegerParentType;
	friend class TinyintType;
	friend class SmallintType;
	friend class IntegerType;
	friend class BigintType;
	friend class DecimalType;
	friend class BooleanType;
	friend class VarcharType; 

	explicit Value(const TypeId type) : manage_data_(false), type_id_(type) { size_.len_ = BUSTUB_VALUE_NULL; }
	Value(TypeId type, int8_t i);
	// BOOLEAN and TINYINT
	Value(TypeId type, int8_t i);
	// DECIMAL
	Value(TypeId type, double d);
	Value(TypeId type, float f);
	// SMALLINT
	Value(TypeId type, int16_t i);
	// INTEGER
	Value(TypeId type, int32_t i);
	// BIGINT
	Value(TypeId type, int64_t i);
	// TIMESTAMP
	Value(TypeId type, uint64_t i);
	// VARCHAR
	Value(TypeId type, const char *data, uint32_t len, bool manage_data);
	Value(TypeId type, const std::string &data);

	Value() : Value(TypeId::INVALID) {}
	Value(const Value &other);
	auto operator=(Value other) -> Value &;
	~Value();

	inline auto GetTypeId() const -> TypeId { return type_id_; }
	
	// Comparison Methods
	inline auto CompareEquals(const Value &o) const -> CmpBool {
		return Type::GetInstance(type_id_)->CompareEquals(*this, o);
	}
	inline auto CompareNotEquals(const Value &o) const -> CmpBool {
		return Type::GetInstance(type_id_)->CompareNotEquals(*this, o);
	}
	inline auto CompareLessThan(const Value &o) const -> CmpBool {
		return Type::GetInstance(type_id_)->CompareLessThan(*this, o);
	}
	inline auto CompareLessThanEquals(const Value &o) const -> CmpBool {
		return Type::GetInstance(type_id_)->CompareLessThanEquals(*this, o);
	}
	inline auto CompareGreaterThan(const Value &o) const -> CmpBool {
		return Type::GetInstance(type_id_)->CompareGreaterThan(*this, o);
	}
	inline auto CompareGreaterThanEquals(const Value &o) const -> CmpBool {
		return Type::GetInstance(type_id_)->CompareGreaterThanEquals(*this, o);
	}

		// Other mathematical functions
	inline auto Add(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Add(*this, o); }
	inline auto Subtract(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Subtract(*this, o); }
	inline auto Multiply(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Multiply(*this, o); }
	inline auto Divide(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Divide(*this, o); }
	inline auto Modulo(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Modulo(*this, o); }
	inline auto Min(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Min(*this, o); }
	inline auto Max(const Value &o) const -> Value { return Type::GetInstance(type_id_)->Max(*this, o); }
	inline auto Sqrt() const -> Value { return Type::GetInstance(type_id_)->Sqrt(*this); }

	inline auto OperateNull(const Value &o) const -> Value { return Type::GetInstance(type_id_)->OperateNull(*this, o); }
	inline auto IsZero() const -> bool { return Type::GetInstance(type_id_)->IsZero(*this); }
	inline auto IsNull() const -> bool { return size_.len_ == BUSTUB_VALUE_NULL; }

	// Serialize this value into the given storage space. The inlined parameter
	// indicates whether we are allowed to inline this value into the storage
	// space, or whether we must store only a reference to this value. If inlined
	// is false, we may use the provided data pool to allocate space for this
	// value, storing a reference into the allocated pool space in the storage.
	inline void SerializeTo(char *storage) const { Type::GetInstance(type_id_)->SerializeTo(*this, storage); }

	// Deserialize a value of the given type from the given storage space.
	inline static auto DeserializeFrom(const char *storage, const TypeId type_id) -> Value {
		return Type::GetInstance(type_id)->DeserializeFrom(storage);
	}
protected:
	union Val {
		int8_t boolean_;
		int8_t tinyint_;
		int16_t smallint_;
		int32_t integer_;
		int64_t bigint_;
		double decimal_;
		uint64_t timestamp_;
		char *varlen_;
		const char *const_varlen_;
	} value_;

	union {
		uint32_t len_;
		TypeId elem_type_id_;
	} size_;

	bool manage_data_;
	TypeId type_id_;
};
