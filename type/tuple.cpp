#include "tuple.h"

#include <cstring>
#include <utility>

Tuple::Tuple(const Schema *schema, std::vector<Value> values)
    : data(std::move(values)), schema(schema) {}

const Schema *Tuple::GetSchema() const {
    return schema;
}

const std::vector<Value> &Tuple::GetValues() const {
    return data;
}

const Value &Tuple::GetField(uint32_t col_idx) const {
    return data[col_idx];
}

void Tuple::SetField(uint32_t col_idx, Value v) {
    data[col_idx] = std::move(v);
}

// Layout: [fixed section: schema->GetFixedSize() bytes][variable tail]
//   fixed section — per column in order:
//     non-VARCHAR: full value bytes (width bytes)
//     VARCHAR:     uint16_t length only (2 bytes)
//   variable tail — per VARCHAR column in order:
//     raw string bytes (no length prefix; length is already in the fixed section)
uint16_t Tuple::Serialize(uint8_t *buf) const {
    uint8_t *fixed_cursor = buf;
    uint8_t *var_cursor   = buf + schema->GetFixedSize();
        //please test these TWO LINE UP PERFECTLY!
    for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); ++col_idx) {
        const Value  &value = data[col_idx];
        const Column &col   = schema->GetColumn(col_idx);

        if (col.IsVariableLength()) {
            std::memcpy(fixed_cursor, &value.val.varchar.len, sizeof(varchar_len_t));
            fixed_cursor += sizeof(varchar_len_t);
            std::memcpy(var_cursor, value.val.varchar.data, value.val.varchar.len);
            var_cursor += value.val.varchar.len;
        } else {
            Type *type = Type::GetInstance(value.type_id);
            type->Serialize(value, fixed_cursor);
            fixed_cursor += type->SerializedSize(value);
        }
    }

    return static_cast<uint16_t>(var_cursor - buf);
}

Tuple Tuple::Deserialize(const Schema *schema, const uint8_t *buf) {
    std::vector<Value> values;
    values.reserve(schema->GetColumnCount());

    const uint8_t *fixed_cursor = buf;
    const uint8_t *var_cursor   = buf + schema->GetFixedSize();

    for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); ++col_idx) {
        const Column &col = schema->GetColumn(col_idx);

        if (col.IsVariableLength()) {
            varchar_len_t len;
            std::memcpy(&len, fixed_cursor, sizeof(varchar_len_t));
            fixed_cursor += sizeof(varchar_len_t);
            values.push_back(Value::make_varchar_owning(
                reinterpret_cast<const char *>(var_cursor), len));
            var_cursor += len;
        } else {
            Type *type = Type::GetInstance(col.GetType());
            Value value = type->Deserialize(fixed_cursor, static_cast<uint8_t>(col.GetLength()));
            fixed_cursor += type->SerializedSize(value);
            values.push_back(std::move(value));
        }
    }

    return Tuple(schema, std::move(values));
}