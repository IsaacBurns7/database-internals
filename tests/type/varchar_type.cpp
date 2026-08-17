#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "type/type.h"
#include "type/varchar_type.h"

TEST(VarcharTypeTest, CompareUsesLexicographicOrderThenLength) {
    VarcharType type;

    const char alpha[] = "alpha";
    const char alphabet[] = "alphabet";
    const char beta[] = "beta";

    Value shorter = Value::make_varchar_nonowning(alpha, 5);
    Value longer = Value::make_varchar_nonowning(alphabet, 8);
    Value later = Value::make_varchar_nonowning(beta, 4);

    EXPECT_LT(type.Compare(shorter, longer), 0);
    EXPECT_GT(type.Compare(longer, shorter), 0);
    EXPECT_LT(type.Compare(shorter, later), 0);
    EXPECT_EQ(type.Compare(shorter, shorter), 0);
}

TEST(VarcharTypeTest, SerializeAndDeserializeRoundTripPayload) {
    VarcharType type;
    const char payload[] = "database";
    Value input = Value::make_varchar_nonowning(payload, 8);

    std::array<uint8_t, sizeof(varchar_len_t) + 8> buffer{};
    EXPECT_EQ(type.SerializedSize(input), buffer.size());
    type.Serialize(input, buffer.data());

    varchar_len_t stored_len = 0;
    std::memcpy(&stored_len, buffer.data(), sizeof(stored_len));
    EXPECT_EQ(stored_len, 8);

    Value output = type.Deserialize(buffer.data(), 0);
    EXPECT_EQ(output.type_id, TypeId::VARCHAR);
    EXPECT_EQ(output.val.varchar.len, 8);
    EXPECT_EQ(std::string(output.val.varchar.data, output.val.varchar.len), "database");
    EXPECT_TRUE(output.val.varchar.owns_data);
}
