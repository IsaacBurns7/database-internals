class BooleanType: public Type {
public:
    // Compare is width-agnostic because we always store as int64
    int Compare(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::BOOLEAN && b.type_id == TypeId::BOOLEAN);
        return (int) a.val.boolean - (int)b.val.boolean;
    }
    uint16_t SerializedSize(const Value &v) const override {
        return 1;
    }
    void Serialize(const Value &v, uint8_t *buf) const override {
        bool x = v.val.boolean;
        memcpy(buf, &x, 1);
    }
    Value Deserialize(const uint8_t *buf, uint8_t width = 1) const override {
        bool x = 0;
        memcpy(&x, buf, width);
        return Value::make_bool(x);
    }
};
