#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "type/column.h"
#include "type/schema.h"

namespace {

template <typename T>
auto ReadPod(const uint8_t *buf) -> T {
    T value{};
    std::memcpy(&value, buf, sizeof(T));
    return value;
}

}  // namespace

TEST(SchemaTest, BasicMetadataAndLookupsWork) {
    Schema schema({
        Column("id", TypeId::NUMERIC, 8, 0),
        Column("name", TypeId::VARCHAR, 8),
        Column("active", TypeId::BOOLEAN, 1, 12),
    });

    EXPECT_EQ(schema.GetColumnCount(), 3u);
    EXPECT_TRUE(schema.HasVariableLengthColumns());
    EXPECT_EQ(schema.GetFixedSize(), 8u + sizeof(varchar_len_t) + 1u);
    EXPECT_EQ(schema.GetColIdx("name"), 1u);
    EXPECT_FALSE(schema.GetColIdx("missing").has_value());

    EXPECT_EQ(schema.GetColumn(0).GetName(), "id");
    EXPECT_EQ(schema.GetColumn(1).GetType(), TypeId::VARCHAR);
    EXPECT_EQ(schema.GetColumn(2).GetOffset(), 12u);
}

TEST(SchemaTest, SerializeLayoutIsCountPrefixedAndRoundTrips) {
    Schema schema({
        Column("id", TypeId::NUMERIC, 8, 0),
        Column("name", TypeId::VARCHAR, 8),
        Column("active", TypeId::BOOLEAN, 1, 12),
    });

    std::array<uint8_t, 256> buffer{};
    const auto written = schema.SerializeSchema(buffer.data());
    EXPECT_GT(written, 0u);

    const auto column_count = ReadPod<uint32_t>(buffer.data());
    EXPECT_EQ(column_count, 3u);

    std::size_t offset = sizeof(uint32_t);
    std::size_t column_consumed = 0;

    Column first = Column::Deserialize(buffer.data() + offset, &column_consumed);
    EXPECT_EQ(first.GetName(), "id");
    EXPECT_EQ(first.GetType(), TypeId::NUMERIC);
    EXPECT_EQ(first.GetLength(), 8u);
    EXPECT_EQ(first.GetOffset(), 0u);
    offset += column_consumed;

    Column second = Column::Deserialize(buffer.data() + offset, &column_consumed);
    EXPECT_EQ(second.GetName(), "name");
    EXPECT_EQ(second.GetType(), TypeId::VARCHAR);
    EXPECT_EQ(second.GetLength(), sizeof(varchar_len_t));
    EXPECT_EQ(second.GetOffset(), 8u);
    offset += column_consumed;

    Column third = Column::Deserialize(buffer.data() + offset, &column_consumed);
    EXPECT_EQ(third.GetName(), "active");
    EXPECT_EQ(third.GetType(), TypeId::BOOLEAN);
    EXPECT_EQ(third.GetLength(), 1u);
    EXPECT_EQ(third.GetOffset(), 12u);
    offset += column_consumed;

    EXPECT_EQ(written, offset);

    std::size_t consumed = 0;
    Schema roundtrip = Schema::Deserialize(buffer.data(), &consumed);
    EXPECT_EQ(consumed, written);
    EXPECT_EQ(roundtrip.GetColumnCount(), schema.GetColumnCount());
    EXPECT_EQ(roundtrip.GetFixedSize(), schema.GetFixedSize());
    EXPECT_EQ(roundtrip.GetColumn(0).GetName(), "id");
    EXPECT_EQ(roundtrip.GetColumn(1).GetType(), TypeId::VARCHAR);
    EXPECT_EQ(roundtrip.GetColumn(2).GetName(), "active");
}

TEST(SchemaTest, DeserializeCanWalkSchemasBackToBack) {
    Schema first({
        Column("a", TypeId::NUMERIC, 4, 0),
    });
    Schema second({
        Column("b", TypeId::VARCHAR, 4),
        Column("c", TypeId::FLOAT, 8, 4),
    });

    std::vector<uint8_t> buffer(256);
    uint8_t *cursor = buffer.data();
    const auto first_written = first.SerializeSchema(cursor);
    cursor += first_written;
    const auto second_written = second.SerializeSchema(cursor);
    cursor += second_written;

    std::size_t first_consumed = 0;
    Schema first_roundtrip = Schema::Deserialize(buffer.data(), &first_consumed);
    EXPECT_EQ(first_consumed, first_written);
    EXPECT_EQ(first_roundtrip.GetColumnCount(), 1u);
    EXPECT_EQ(first_roundtrip.GetColumn(0).GetName(), "a");

    std::size_t second_consumed = 0;
    Schema second_roundtrip = Schema::Deserialize(buffer.data() + first_consumed, &second_consumed);
    EXPECT_EQ(second_consumed, second_written);
    EXPECT_EQ(second_roundtrip.GetColumnCount(), 2u);
    EXPECT_EQ(second_roundtrip.GetColumn(0).GetName(), "b");
    EXPECT_EQ(second_roundtrip.GetColumn(1).GetType(), TypeId::FLOAT);
}
