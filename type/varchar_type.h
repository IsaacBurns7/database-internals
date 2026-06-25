#pragma once 

#define MAX_VARCHAR_LEN 64 

class VarcharType: public Type {
public:
    using varchar_len_t = uint16_t; //maybe add to the like common.config or whatever later 
    int Compare(const Value &a, const Value &b) const override {
        assert(a.type_id == TypeId::VARCHAR && b.type_id == TypeId::VARCHAR);
		uint16_t comparable_len = std::min(a.val.varchar.len, b.val.varchar.len);
		for(uint16_t i = 0;i < comparable_len;i++){
			unsigned char ca = (unsigned char)a.val.varchar.data[i]; 
			unsigned char cb = (unsigned char)b.val.varchar.data[i]; 
			if(ca > cb) return 1; 
			if(ca < cb) return -1; 
		}
		// common prefix is equal — longer string wins
		if (a.val.varchar.len < b.val.varchar.len) return -1;
		if (a.val.varchar.len > b.val.varchar.len) return  1;
		return 0;
    }

	//neither of the below functions are sensible because VarcharType does not own the lifetime of its own data 
		// Value Add(const Value &a, const Value &b) const override 
		// Value Sub(const Value &a, const Value &b) const override
	
	uint16_t SerializedSize(const Value &v) const override {
        return v.val.varchar.len + sizeof(v.val.varchar.len);
    }

    void Serialize(const Value &v, uint8_t *buf) const override {
		uint8_t len_t_size = sizeof(varchar_len_t);
		memcpy(buf, &v.val.varchar.len, len_t_size); //should be uint16_t for now 
		memcpy(buf + len_t_size, v.val.varchar.data, v.val.varchar.len);
    }
	
	//only makes sense with an owning flag or arena (ARENA IS BETTER)
    //memory layout of buf: [length_prefix][data]
    //width is irrelevant
 	Value Deserialize(const uint8_t *buf, uint8_t width = 0) const override {
		//in reality we will just makevarchar through the singleton 
        varchar_len_t len;  //should change uint16_t to LEN_T later...  
        memcpy(&len, buf, sizeof(varchar_len_t)); 
        return Value::make_varchar_owning(
            (const char*)(buf + sizeof(varchar_len_t)), 
            len);
    }
};
