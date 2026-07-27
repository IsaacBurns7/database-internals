#include <string>
#include "common/exception.h"
#include "type/bigint_type.h"
#include "type/boolean_type.h"
#include "type/decimal_type.h"
#include "type/integer_type.h"
#include "type/integer_parent_type.h"
#include "type/smallint_type.h"
// #include "type/timestamp_type.h"
#include "type/tinyint_type.h"
#include "type/value.h"

Type *Type::k_types[] = {new Type(TypeId::INVALID, 
		 new BooleanType(),
		 new TinyintType(),
		 new SmallintType(),
		 new IntegerType(TypeId::INTEGER),
		 new BigintType(),
		 new DecimalType(),
		 new VarcharType()
	};

auto Type::CompareEquals(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> CmpBool {
  throw NotImplementedException("CompareEquals not implemented");
}

auto Type::CompareNotEquals(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> CmpBool {
  throw NotImplementedException("CompareNotEquals not implemented");
}

auto Type::CompareLessThan(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> CmpBool {
  throw NotImplementedException("CompareLessThan not implemented");
}
auto Type::CompareLessThanEquals(const Value &left __attribute__((unused)),
                                 const Value &right __attribute__((unused))) const -> CmpBool {
  throw NotImplementedException("CompareLessThanEqual not implemented");
}
auto Type::CompareGreaterThan(const Value &left __attribute__((unused)),
                              const Value &right __attribute__((unused))) const -> CmpBool {
  throw NotImplementedException("CompareGreaterThan not implemented");
}
auto Type::CompareGreaterThanEquals(const Value &left __attribute__((unused)),
                                    const Value &right __attribute__((unused))) const -> CmpBool {
  throw NotImplementedException("CompareGreaterThanEqual not implemented");
}

// Other mathematical functions
auto Type::Add(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const -> Value {
  throw NotImplementedException("Add not implemented");
}

auto Type::Subtract(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> Value {
  throw NotImplementedException("Subtract not implemented");
}

auto Type::Multiply(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> Value {
  throw NotImplementedException("Multiply not implemented");
}

auto Type::Divide(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> Value {
  throw NotImplementedException("Divide not implemented");
}

auto Type::Modulo(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> Value {
  throw NotImplementedException("Modulo not implemented");
}

auto Type::Min(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const -> Value {
  throw NotImplementedException("Min not implemented");
}

auto Type::Max(const Value &left __attribute__((unused)), const Value &right __attribute__((unused))) const -> Value {
  throw NotImplementedException("Max not implemented");
}

auto Type::Sqrt(const Value &val __attribute__((unused))) const -> Value {
  throw NotImplementedException("Sqrt not implemented");
}

auto Type::OperateNull(const Value &val __attribute__((unused)), const Value &right __attribute__((unused))) const
    -> Value {
  throw NotImplementedException("OperateNull not implemented");
}

auto Type::IsZero(const Value &val __attribute__((unused))) const -> bool {
  throw NotImplementedException("isZero not implemented");
}

virtual void Type::SerializeTo(const Value &val __attribute__((unused)), char *storage __attribute__((unused)) ) const {
	throw NotImplementedException("SerializeTo not implemented");
};

virtual auto Type::DeserializeFrom(const char *storage __attribute__((unused)) ) const -> Value {
	throw NotImplementedException("DeserializeFrom not implemented");
}
	
