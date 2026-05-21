#pragma once

#include <string>
#include "common/exception.h"
#include "type/value.h"

class IntegerParentType {

public:
	~IntegerParentType() override = default;
	explicit IntegerParentType(TypeId type);
	
	// Other mathematical functions
	auto Add(const Value &left, const Value &right) const -> Value override = 0;
	auto Subtract(const Value &left, const Value &right) const -> Value override = 0;
	auto Multiply(const Value &left, const Value &right) const -> Value override = 0;
	auto Divide(const Value &left, const Value &right) const -> Value override = 0;
	auto Modulo(const Value &left, const Value &right) const -> Value override = 0;
	auto Min(const Value &left, const Value &right) const -> Value override;
	auto Max(const Value &left, const Value &right) const -> Value override;
	auto Sqrt(const Value &val) const -> Value override = 0;

	// Comparison functions
	auto CompareEquals(const Value &left, const Value &right) const -> CmpBool override = 0;
	auto CompareNotEquals(const Value &left, const Value &right) const -> CmpBool override = 0;
	auto CompareLessThan(const Value &left, const Value &right) const -> CmpBool override = 0;
	auto CompareLessThanEquals(const Value &left, const Value &right) const -> CmpBool override = 0;
	auto CompareGreaterThan(const Value &left, const Value &right) const -> CmpBool override = 0;
	auto CompareGreaterThanEquals(const Value &left, const Value &right) const -> CmpBool override = 0;

	void SerializeTo(const Value &val, char *storage) const override;
	auto DeserializeFrom(const char *storage) const -> Value override;
protected:
	auto OperateNull(const Value &left, const Value &right) const -> Value override = 0;
	auto IsZero(const Value &val) const -> bool override = 0;
	
	template <class T1, class T2>
	auto AddValue(const Value &left, const Value &right) const -> Value;
	template <class T1, class T2>
	auto SubtractValue(const Value &left, const Value &right) const -> Value;
	template <class T1, class T2>
	auto MultiplyValue(const Value &left, const Value &right) const -> Value;
	template <class T1, class T2>
	auto DivideValue(const Value &left, const Value &right) const -> Value;
	template <class T1, class T2>
	auto ModuloValue(const Value &left, const Value &right) const -> Value;
};

template <class T1, class T2>
auto IntegerParentType::AddValue(const Value& left, const Value& right) const -> Value {
	auto x = left.GetAs<T1>();
	auto y = right.GetAs<T2>();
	auto sum1 = static_cast<T1>(x+y);
	auto sum2 = static_cast<T2>(x+y); 
	
}
