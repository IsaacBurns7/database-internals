class VarcharType: public Type {
public:
    int Compare(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::VARCHAR && b.type_id == TypeId::VARCHAR);
		uint16_t comparable_len = std::min(a.varchar.len, b.varchar.len);
		for(uint16_t i = 0;i < comparable_len;i++){
			unsigned char ca = (unsigned char)a.varchar.data[i]; 
			unsigned char cb = (unsigned char)b.varchar.data[i]; 
			if(ca > cb) return 1; 
			if(ca < cb) return -1; 
		}
		// common prefix is equal — longer string wins
		if (a.varchar.len < b.varchar.len) return -1;
		if (a.varchar.len > b.varchar.len) return  1;
		return 0;
    }

	//neither of the below functions are sensible because VarcharType does not own the lifetime of its own data 
		// Value Add(const Value &a, const Value &b) const override 
		// Value Sub(const Value &a, const Value &b) const override

	uint16_t SerializedSize(const Value &v) const override {
        return v.varchar.len;
    }

    void Serialize(const Value &v, uint8_t *buf) const override {
        memcpy(buf, &v.varchar.data, v.varchar.len);
    }
	
	//only makes sense with an owning flag or arena (ARENA IS BETTER)
 	   // Value Deserialize(const uint8_t *buf, uint8_t width) const override {
};
