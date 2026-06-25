#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "type/type.h"
#include "type/float_type.h"

TEST(FloatTypeTest, CompareAndArithmeticUseFloatingPointSemantics) {
    FloatType type;
    Value lhs = Value::make_float(1.5, 8);
    Value rhs = Value::make_float(2.25, 8);

    EXPECT_LT(type.Compare(lhs, rhs), 0);
    EXPECT_GT(type.Compare(rhs, lhs), 0);
    EXPECT_EQ(type.Compare(lhs, lhs), 0);

    Value sum = type.Add(lhs, rhs);
    Value diff = type.Sub(rhs, lhs);

    EXPECT_EQ(sum.type_id, TypeId::FLOAT);
    EXPECT_EQ(sum.width, 8);
    EXPECT_DOUBLE_EQ(sum.val.fp, 3.75);
    EXPECT_EQ(diff.width, 8);
    EXPECT_DOUBLE_EQ(diff.val.fp, 0.75);
}

TEST(FloatTypeTest, SerializeRoundTripsForFloatAndDoubleWidths) {
    FloatType type;

    const Value double_value = Value::make_float(123.5, 8);
    std::array<uint8_t, 8> double_buffer{};
    EXPECT_EQ(type.SerializedSize(double_value), 8);
    type.Serialize(double_value, double_buffer.data());
    Value double_roundtrip = type.Deserialize(double_buffer.data(), 8);
    EXPECT_EQ(double_roundtrip.width, 8);
    EXPECT_DOUBLE_EQ(double_roundtrip.val.fp, 123.5);

    const Value float_value = Value::make_float(3.25, 4);
    std::array<uint8_t, 4> float_buffer{};
    EXPECT_EQ(type.SerializedSize(float_value), 4);
    type.Serialize(float_value, float_buffer.data());
    Value float_roundtrip = type.Deserialize(float_buffer.data(), 4);
    EXPECT_EQ(float_roundtrip.width, 4);
    EXPECT_DOUBLE_EQ(float_roundtrip.val.fp, 3.25);
}
