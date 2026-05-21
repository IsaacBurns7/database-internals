// type.cpp

static NumericType  numeric_instance;
static FloatType    float_instance;
static BooleanType  boolean_instance;
static VarcharType  varchar_instance;

Type *Type::GetInstance(TypeId id) {
    switch (id) {
        case TypeId::NUMERIC: return &numeric_instance;
        case TypeId::FLOAT:   return &float_instance;
        case TypeId::BOOLEAN: return &boolean_instance;
        case TypeId::VARCHAR: return &varchar_instance;
        default: assert(false && "unknown TypeId");
    }
}
