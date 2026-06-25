#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "type/type.h"
#include "type/boolean_type.h"

TEST(BooleanTypeTest, CompareOrdersFalseBeforeTrue) {
    BooleanType type;
    Value false_value = Value::make_bool(false);
    Value true_value = Value::make_bool(true);

    EXPECT_LT(type.Compare(false_value, true_value), 0);
    EXPECT_GT(type.Compare(true_value, false_value), 0);
    EXPECT_EQ(type.Compare(false_value, false_value), 0);
}

TEST(BooleanTypeTest, SerializeRoundTripsSingleByte) {
    BooleanType type;
    Value input = Value::make_bool(true);
    std::array<uint8_t, 1> buffer{};

    EXPECT_EQ(type.SerializedSize(input), 1);
    type.Serialize(input, buffer.data());

    Value output = type.Deserialize(buffer.data(), 1);
    EXPECT_EQ(output.type_id, TypeId::BOOLEAN);
    EXPECT_TRUE(output.val.boolean);
}
