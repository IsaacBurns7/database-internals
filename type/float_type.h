// numeric_type.h  — handles ALL fp widths, no repetition

class FloatType: public Type {
public:
    int Compare(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::FLOAT && b.type_id == TypeId::FLOAT);
        if (a.val.fp < b.val.fp) return -1;
        if (a.val.fp > b.val.fp) return  1;
        return 0;
    }
    Value Add(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::FLOAT && b.type_id == TypeId::FLOAT);
        // result takes the wider type
        uint8_t w = a.width > b.width ? a.width : b.width;
        return Value::make_float(a.val.fp + b.val.fp, w);
    }
    Value Sub(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::FLOAT && b.type_id == TypeId::FLOAT);
        uint8_t w = a.width > b.width ? a.width : b.width;
        return Value::make_float(a.val.fp - b.val.fp, w);
    }
    uint16_t SerializedSize(const Value &v) const override {
        return v.width;
    }
    void Serialize(const Value &v, uint8_t *buf) const override {
        if(v.width == 4){
			float f = (float)v.val.fp; 
			memcpy(buf, &f, 4);
		}else{
			double d = (double)v.val.fp; 
			memcpy(buf, &d, 8);
		}
    }
    Value Deserialize(const uint8_t *buf, uint8_t width) const override {
		if (width == 4) {
			float f;
			memcpy(&f, buf, 4);
			return Value::make_float((double)f, 4);
		} else {
			// width == 8
			double d;
			memcpy(&d, buf, 8);
			return Value::make_float(d, 8);
		}
    }
};
