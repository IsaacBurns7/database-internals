#include "tuple.h"

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

uint16_t Tuple::Serialize(uint8_t *buf) const {
    uint8_t *cursor = buf;
    for (const Value &value : data) {
        Type::GetInstance(value.type_id)->Serialize(value, cursor);
        cursor += Type::GetInstance(value.type_id)->SerializedSize(value);
    }
    return static_cast<uint16_t>(cursor - buf);
}

Tuple Tuple::Deserialize(const Schema *schema, const uint8_t *buf) {
    std::vector<Value> values;
    values.reserve(schema->GetColumnCount());

    const uint8_t *cursor = buf;
    for (uint32_t col_idx = 0; col_idx < schema->GetColumnCount(); ++col_idx) {
        const Column &column = schema->GetColumn(col_idx);
        Type *type = Type::GetInstance(column.GetType());
        Value value = type->Deserialize(cursor, static_cast<uint8_t>(column.GetLength()));
        cursor += type->SerializedSize(value);
        values.push_back(std::move(value));
    }

    return Tuple(schema, std::move(values));
}