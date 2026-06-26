#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "type/column.h"

namespace {

template <typename T>
auto ReadPod(const uint8_t *buf) -> T {
    T value{};
    std::memcpy(&value, buf, sizeof(T));
    return value;
}

}  // namespace

TEST(ColumnTest, SerializeLayoutForFixedLengthColumnIsStable) {
    Column column("age", TypeId::NUMERIC, 4, 12);
    std::array<uint8_t, 3 + 2 + 1 + 4 + 4 + 8> buffer{};

    const auto written = column.Serialize(buffer.data());
    EXPECT_EQ(written, 2u + 3u + 1u + 4u + 4u);

    const auto name_len = ReadPod<uint16_t>(buffer.data());
    EXPECT_EQ(name_len, 3u);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(buffer.data() + sizeof(uint16_t)), name_len), "age");

    const auto type = ReadPod<uint8_t>(buffer.data() + sizeof(uint16_t) + name_len);
    EXPECT_EQ(type, static_cast<uint8_t>(TypeId::NUMERIC));

    const auto length = ReadPod<uint32_t>(buffer.data() + sizeof(uint16_t) + name_len + sizeof(uint8_t));
    const auto offset = ReadPod<uint32_t>(buffer.data() + sizeof(uint16_t) + name_len + sizeof(uint8_t) + sizeof(uint32_t));
    EXPECT_EQ(length, 4u);
    EXPECT_EQ(offset, 12u);

    std::size_t consumed = 0;
    Column roundtrip = Column::Deserialize(buffer.data(), &consumed);
    EXPECT_EQ(consumed, written);
    EXPECT_EQ(roundtrip.GetName(), "age");
    EXPECT_EQ(roundtrip.GetType(), TypeId::NUMERIC);
    EXPECT_EQ(roundtrip.GetLength(), 4u);
    EXPECT_EQ(roundtrip.GetOffset(), 12u);
    EXPECT_FALSE(roundtrip.IsVariableLength());
}

TEST(ColumnTest, VarcharColumnUsesPrefixLengthInSerializedMetadata) {
    Column column("nickname", TypeId::VARCHAR, 24);
    std::array<uint8_t, 2 + 8 + 1 + 4 + 4> buffer{};

    const auto written = column.Serialize(buffer.data());
    EXPECT_EQ(written, 2u + 8u + 1u + 4u + 4u);

    const auto name_len = ReadPod<uint16_t>(buffer.data());
    EXPECT_EQ(name_len, 8u);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(buffer.data() + sizeof(uint16_t)), name_len), "nickname");

    const auto type = ReadPod<uint8_t>(buffer.data() + sizeof(uint16_t) + name_len);
    EXPECT_EQ(type, static_cast<uint8_t>(TypeId::VARCHAR));

    const auto length = ReadPod<uint32_t>(buffer.data() + sizeof(uint16_t) + name_len + sizeof(uint8_t));
    const auto offset = ReadPod<uint32_t>(buffer.data() + sizeof(uint16_t) + name_len + sizeof(uint8_t) + sizeof(uint32_t));
    EXPECT_EQ(length, sizeof(varchar_len_t));
    EXPECT_EQ(offset, 24u);

    std::size_t consumed = 0;
    Column roundtrip = Column::Deserialize(buffer.data(), &consumed);
    EXPECT_EQ(consumed, written);
    EXPECT_EQ(roundtrip.GetName(), "nickname");
    EXPECT_EQ(roundtrip.GetType(), TypeId::VARCHAR);
    EXPECT_EQ(roundtrip.GetLength(), sizeof(varchar_len_t));
    EXPECT_EQ(roundtrip.GetOffset(), 24u);
    EXPECT_TRUE(roundtrip.IsVariableLength());
}

TEST(ColumnTest, DeserializeCanWalkMultipleColumnsBackToBack) {
    Column first("id", TypeId::NUMERIC, 8, 0);
    Column second("label", TypeId::VARCHAR, 8);

    std::vector<uint8_t> buffer(128);
    uint8_t *cursor = buffer.data();
    const auto first_written = first.Serialize(cursor);
    cursor += first_written;
    const auto second_written = second.Serialize(cursor);
    cursor += second_written;

    std::size_t consumed_first = 0;
    Column first_roundtrip = Column::Deserialize(buffer.data(), &consumed_first);
    EXPECT_EQ(consumed_first, first_written);
    EXPECT_EQ(first_roundtrip.GetName(), "id");

    std::size_t consumed_second = 0;
    Column second_roundtrip = Column::Deserialize(buffer.data() + consumed_first, &consumed_second);
    EXPECT_EQ(consumed_second, second_written);
    EXPECT_EQ(second_roundtrip.GetName(), "label");
    EXPECT_EQ(second_roundtrip.GetType(), TypeId::VARCHAR);
}
