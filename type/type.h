// type.h

#pragma once
#include "value.h"

class Type {
public:
    virtual ~Type() = default;

    // The three things the B+ Tree actually needs
    virtual int  Compare(const Value &a, const Value &b) const = 0;

    // Arithmetic — only defined where it makes sense
    virtual Value Add(const Value &a, const Value &b) const {
        throw std::logic_error("Add not supported for this type");
    }
    virtual Value Sub(const Value &a, const Value &b) const {
        throw std::logic_error("Sub not supported for this type");
    }

    // Serialization
    virtual uint16_t SerializedSize(const Value &v) const = 0;
    virtual void     Serialize(const Value &v, uint8_t *buf) const = 0;
    virtual Value    Deserialize(const uint8_t *buf, uint8_t width) const = 0;

    // Registry
    static Type *GetInstance(TypeId id);
};
