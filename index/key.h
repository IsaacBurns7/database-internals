#pragma once

#include <cstdint>

#include "type/type.h"

// Non-owning view over a key's bytes. Caller must ensure the backing buffer
// outlives this Key (typically the page buffer it was read from).
class Key {
public:
    Key(TypeId type_id, uint8_t width, const uint8_t* data, uint16_t size)
        : type_id_(type_id), width_(width), data_(data), size_(size) {}

    static auto FromBytes(TypeId type_id, uint8_t width, const uint8_t* data, uint16_t size) -> Key {
        return Key(type_id, width, data, size);
    }

    //this is dicey b/c the caller owns the data_ 
    auto ToValue() const -> Value {
        Type* type = Type::GetInstance(type_id_);
        return type->Deserialize(data_, width_);
    }

    auto Compare(const Key& other) const -> int {
        Type* type = Type::GetInstance(type_id_);
        return type->Compare(ToValue(), other.ToValue());
    }

    auto GetTypeId() const -> TypeId { return type_id_; }
    auto GetWidth() const -> uint8_t { return width_; }
    auto GetData() const -> const uint8_t* { return data_; }
    auto GetSize() const -> uint16_t { return size_; }

private:
    TypeId type_id_;
    uint8_t width_;
    const uint8_t* data_;
    uint16_t size_;
};