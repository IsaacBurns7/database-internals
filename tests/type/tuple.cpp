#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "type/column.h"
#include "type/schema.h"
#include "type/tuple.h"
#include "type/value.h"

namespace {

auto MakeFixedSchema() -> Schema {
    return Schema({
        Column("id", TypeId::NUMERIC, 8, 0),
        Column("active", TypeId::BOOLEAN, 1, 8),
        Column("score", TypeId::FLOAT, 8, 9),
    });
}

auto MakeVarcharSchema() -> Schema {
    return Schema({
        Column("id", TypeId::NUMERIC, 4, 0),
        Column("name", TypeId::VARCHAR, 4),
        Column("active", TypeId::BOOLEAN, 1, 8),
    });
}

}  // namespace

TEST(TupleTest, GetFieldAndSetFieldReflectConstructorValues) {
    Schema schema = MakeFixedSchema();
    Tuple tuple(&schema, {
        Value::make_int(42, 8),
        Value::make_bool(true),
        Value::make_float(3.5, 8),
    });

    EXPECT_EQ(tuple.GetSchema(), &schema);
    EXPECT_EQ(tuple.GetValues().size(), 3u);
    EXPECT_EQ(tuple.GetField(0).val.integer, 42);
    EXPECT_EQ(tuple.GetField(1).val.boolean, true);
    EXPECT_DOUBLE_EQ(tuple.GetField(2).val.fp, 3.5);

    tuple.SetField(0, Value::make_int(99, 8));
    EXPECT_EQ(tuple.GetField(0).val.integer, 99);
}

TEST(TupleTest, SerializeFixedOnlySchemaWritesPackedFieldsAndNoTail) {
    Schema schema = MakeFixedSchema();
    Tuple tuple(&schema, {
        Value::make_int(7, 8),
        Value::make_bool(true),
        Value::make_float(2.0, 8),
    });

    std::array<uint8_t, 64> buffer{};
    uint16_t written = tuple.Serialize(buffer.data());

    EXPECT_EQ(written, schema.GetFixedSize());

    int64_t id = 0;
    std::memcpy(&id, buffer.data(), sizeof(int64_t));
    EXPECT_EQ(id, 7);

    EXPECT_EQ(buffer[8], 1);

    double score = 0.0;
    std::memcpy(&score, buffer.data() + 9, sizeof(double));
    EXPECT_DOUBLE_EQ(score, 2.0);
}

TEST(TupleTest, SerializeVarcharSchemaWritesLengthPrefixAndTail) {
    Schema schema = MakeVarcharSchema();
    const char payload[] = "hello";
    Tuple tuple(&schema, {
        Value::make_int(1, 4),
        Value::make_varchar_nonowning(payload, 5),
        Value::make_bool(false),
    });

    std::array<uint8_t, 64> buffer{};
    uint16_t written = tuple.Serialize(buffer.data());

    // fixed region: 4 (id) + 2 (varchar len prefix) + 1 (bool) = 7
    // plus variable tail of 5 bytes for "hello"
    EXPECT_EQ(schema.GetFixedSize(), 7u);
    EXPECT_EQ(written, 7u + 5u);

    varchar_len_t stored_len = 0;
    std::memcpy(&stored_len, buffer.data() + 4, sizeof(varchar_len_t));
    EXPECT_EQ(stored_len, 5);

    EXPECT_EQ(std::string(reinterpret_cast<char *>(buffer.data() + 7), 5), "hello");
}

TEST(TupleTest, DeserializeFixedOnlySchemaRoundTrips) {
    Schema schema = MakeFixedSchema();
    Tuple tuple(&schema, {
        Value::make_int(123, 8),
        Value::make_bool(true),
        Value::make_float(9.75, 8),
    });

    std::array<uint8_t, 64> buffer{};
    tuple.Serialize(buffer.data());

    Tuple round_trip = Tuple::Deserialize(&schema, buffer.data());
    EXPECT_EQ(round_trip.GetField(0).val.integer, 123);
    EXPECT_EQ(round_trip.GetField(1).val.boolean, true);
    EXPECT_DOUBLE_EQ(round_trip.GetField(2).val.fp, 9.75);
}

TEST(TupleTest, DeserializeVarcharSchemaRoundTrips) {
    Schema schema = MakeVarcharSchema();
    const char payload[] = "database";
    Tuple tuple(&schema, {
        Value::make_int(5, 4),
        Value::make_varchar_nonowning(payload, 8),
        Value::make_bool(true),
    });

    std::array<uint8_t, 64> buffer{};
    tuple.Serialize(buffer.data());

    Tuple round_trip = Tuple::Deserialize(&schema, buffer.data());
    EXPECT_EQ(round_trip.GetField(0).val.integer, 5);
    ASSERT_EQ(round_trip.GetField(1).val.varchar.len, 8);
    EXPECT_EQ(std::string(round_trip.GetField(1).val.varchar.data,
                          round_trip.GetField(1).val.varchar.len),
              "database");
    EXPECT_EQ(round_trip.GetField(2).val.boolean, true);
}
