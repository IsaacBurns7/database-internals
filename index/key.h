#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "type/type.h"

/*
 * Key is a small helper around the bytes that represent an index key.
 *
 * Design directions you can still take from here:
 * - Keep Key owning its bytes, which makes comparisons safe even if the
 *   source page is evicted or compacted later.
 * - Switch Key to a non-owning span/view later if you want to avoid copies
 *   during internal-node traversal.
 * - If you move toward a more value-centric tree, Key can become a thin
 *   wrapper around Value instead of a byte container.
 * - If you need composite keys later, Key can hold multiple typed fields
 *   and compare them lexicographically through the same type registry.
 */
class Key {
public:
    Key(TypeId type_id, uint8_t width, std::vector<uint8_t> bytes)
        : type_id_(type_id), width_(width), bytes_(std::move(bytes)) {}

    static auto FromBytes(TypeId type_id, uint8_t width, const uint8_t *bytes, uint16_t size) -> Key {
        return Key(type_id, width, std::vector<uint8_t>(bytes, bytes + size));
    }

    static auto FromValue(const Value &value) -> Key {
        Type *type = Type::GetInstance(value.type_id);
        std::vector<uint8_t> bytes(type->SerializedSize(value));
        type->Serialize(value, bytes.data());
        return Key(value.type_id, value.width, std::move(bytes));
    }

    auto ToValue() const -> Value {
        Type *type = Type::GetInstance(type_id_);
        return type->Deserialize(bytes_.data(), width_);
    }

    auto Compare(const Key &other) const -> int {
        Type *type = Type::GetInstance(type_id_);
        return type->Compare(ToValue(), other.ToValue());
    }

    auto GetTypeId() const -> TypeId {
        return type_id_;
    }

    auto GetWidth() const -> uint8_t {
        return width_;
    }

    auto GetBytes() const -> const std::vector<uint8_t> & {
        return bytes_;
    }

private:
    TypeId type_id_;
    uint8_t width_;
    std::vector<uint8_t> bytes_;
};