// numeric_type.h  — handles ALL integer widths, no repetition

class NumericType : public Type {
public:
    // Compare is width-agnostic because we always store as int64
    int Compare(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::NUMERIC && b.type_id == TypeId::NUMERIC);
        if (a.val.integer < b.val.integer) return -1;
        if (a.val.integer > b.val.integer) return  1;
        return 0;
    }

    Value Add(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::NUMERIC && b.type_id == TypeId::NUMERIC);
        // result takes the wider type
        uint8_t w = a.width > b.width ? a.width : b.width;
        return Value::make_int(a.val.integer + b.val.integer, w);
    }

    Value Sub(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::NUMERIC && b.type_id == TypeId::NUMERIC);
        uint8_t w = a.width > b.width ? a.width : b.width;
        return Value::make_int(a.val.integer - b.val.integer, w);
    }

    uint16_t SerializedSize(const Value &v) const override {
        return v.width;   // just the width — 1, 2, 4, or 8 bytes
    }

    void Serialize(const Value &v, uint8_t *buf) const override {
        // always write little-endian, truncated to width
        int64_t x = v.val.integer;
        memcpy(buf, &x, v.width);
    }

    Value Deserialize(const uint8_t *buf, uint8_t width) const override {
        int64_t x = 0;
        memcpy(&x, buf, width);
        // sign-extend
        int shift = (8 - width) * 8;
        if (shift > 0) x = (x << shift) >> shift;
        return Value::make_int(x, width);
    }
};
