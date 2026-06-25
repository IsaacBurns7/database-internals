#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "type/type.h"
#include "type/numeric_type.h"

TEST(NumericTypeTest, CompareAndArithmeticUseIntegerSemantics) {
    NumericType type;
    Value lhs = Value::make_int(-4, 4);
    Value rhs = Value::make_int(7, 4);

    EXPECT_LT(type.Compare(lhs, rhs), 0);
    EXPECT_GT(type.Compare(rhs, lhs), 0);
    EXPECT_EQ(type.Compare(lhs, lhs), 0);

    Value sum = type.Add(lhs, rhs);
    Value diff = type.Sub(rhs, lhs);

    EXPECT_EQ(sum.type_id, TypeId::NUMERIC);
    EXPECT_EQ(sum.width, 4);
    EXPECT_EQ(sum.val.integer, 3);
    EXPECT_EQ(diff.width, 4);
    EXPECT_EQ(diff.val.integer, 11);
}

TEST(NumericTypeTest, SerializeRoundTripsAcrossWidths) {
    NumericType type;

    const std::array<uint8_t, 4> widths = {1, 2, 4, 8};
    const std::array<int64_t, 4> values = {-5, -1234, 123456, -1234567890123LL};

    for (size_t i = 0; i < widths.size(); ++i) {
        Value input = Value::make_int(values[i], widths[i]);
        std::array<uint8_t, 8> buffer{};

        EXPECT_EQ(type.SerializedSize(input), widths[i]);
        type.Serialize(input, buffer.data());

        Value output = type.Deserialize(buffer.data(), widths[i]);
        EXPECT_EQ(output.type_id, TypeId::NUMERIC);
        EXPECT_EQ(output.width, widths[i]);
        EXPECT_EQ(output.val.integer, values[i]);
    }
}

TEST(NumericTypeTest, DeserializeSignExtendsNegativeValues) {
    NumericType type;
    const uint8_t raw = 0xFE; // -2 in int8_t

    Value output = type.Deserialize(&raw, 1);
    EXPECT_EQ(output.val.integer, -2);
    EXPECT_EQ(output.width, 1);
}
