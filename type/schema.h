#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────────────────
//  TypeId  (logical — used by Value/Type dispatch layer)
//
//  NUMERIC covers all integer widths (1, 2, 4, 8 bytes).
//  FLOAT   covers float (4) and double (8).
//  Width is stored separately in FieldDef::value_width and Value::width.
// ─────────────────────────────────────────────────────────────────────────────

typedef enum __attribute__((packed)) {
    TYPEID_INVALID  = 0,
    TYPEID_BOOLEAN,
    TYPEID_NUMERIC,     // int8, int16, int32, int64
    TYPEID_FLOAT,       // float, double
    TYPEID_VARCHAR,
} TypeId;


// ─────────────────────────────────────────────────────────────────────────────
//  FieldType  (physical — used for offset/size math inside the schema)
//
//  This is the enum you set when defining a schema field.
//  schema_build() derives TypeId + value_width from it automatically.
// ─────────────────────────────────────────────────────────────────────────────

typedef enum __attribute__((packed)) {
    TYPE_INVALID = 0,
    TYPE_BOOL,
    TYPE_INT1,      //  1 byte signed
    TYPE_INT2,      //  2 bytes signed
    TYPE_INT4,      //  4 bytes signed
    TYPE_INT8,      //  8 bytes signed
    TYPE_FLOAT4,    //  4 bytes (float)
    TYPE_FLOAT8,    //  8 bytes (double)
    TYPE_CHAR,      //  fixed-length byte string; set FieldDef::max_size
    TYPE_VARCHAR,   //  variable-length;          set FieldDef::max_size
} FieldType;

// Returns true if the field uses the var-length region of a record.
static inline bool field_is_variable(FieldType t) {
    return t == TYPE_VARCHAR;
}

// Bytes this field occupies in the fixed region of a record.
// For VARCHAR this is the descriptor size (offset + length), NOT the data size.
#define VAR_DESC_SIZE 4     // 2B var_offset + 2B var_len

static inline uint16_t field_fixed_size(FieldType t, uint16_t max_size) {
    switch (t) {
        case TYPE_BOOL:    return 1;
        case TYPE_INT1:    return 1;
        case TYPE_INT2:    return 2;
        case TYPE_INT4:    return 4;
        case TYPE_INT8:    return 8;
        case TYPE_FLOAT4:  return 4;
        case TYPE_FLOAT8:  return 8;
        case TYPE_CHAR:    return max_size;   // caller specifies exact width
        case TYPE_VARCHAR: return VAR_DESC_SIZE;
        default: assert(false && "unknown FieldType"); return 0;
    }
}


// ─────────────────────────────────────────────────────────────────────────────
//  VarDesc  (lives in fixed region, points into var region)
//
//  Wire layout inside fixed region:
//    [ var_offset : 2B ][ var_len : 2B ]
//
//  var_offset : byte offset from start of var region to this field's data
//  var_len    : actual byte length of this field's data
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    uint16_t var_offset;
    uint16_t var_len;
} VarDesc;

static inline VarDesc vardesc_read(const uint8_t *ptr) {
    VarDesc d;
    memcpy(&d.var_offset, ptr,     sizeof(uint16_t));
    memcpy(&d.var_len,    ptr + 2, sizeof(uint16_t));
    return d;
}

static inline void vardesc_write(uint8_t *ptr, VarDesc d) {
    memcpy(ptr,     &d.var_offset, sizeof(uint16_t));
    memcpy(ptr + 2, &d.var_len,    sizeof(uint16_t));
}


// ─────────────────────────────────────────────────────────────────────────────
//  FieldDef
//
//  Set `type` and `max_size` (for CHAR/VARCHAR) before calling schema_build().
//  All other fields are computed by schema_build() — do not set them manually.
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    // ── set by caller ────────────────────────────────────────────────────────
    FieldType type;         // physical type
    uint16_t  max_size;     // CHAR: exact width in bytes
                            // VARCHAR: max allowed data bytes
                            // all others: ignored (computed from type)
    char      name[32];     // field name, for debugging / query layer

    // ── computed by schema_build() — do not set manually ────────────────────
    TypeId    type_id;      // logical type for Value/Type dispatch
    uint8_t   value_width;  // width passed into Value::make_* and Deserialize
    uint16_t  fixed_offset; // byte offset of this field in the fixed region
    uint16_t  fixed_size;   // byte width of this field in the fixed region
} FieldDef;


// ─────────────────────────────────────────────────────────────────────────────
//  fielddef_set_type()
//
//  Derives type_id and value_width from a FieldType.
//  Called internally by schema_build(); you should not need to call this
//  directly unless you are constructing a FieldDef outside a Schema.
// ─────────────────────────────────────────────────────────────────────────────

static inline void fielddef_set_type(FieldDef *f, FieldType ft) {
    assert(f != NULL);
    f->type = ft;
    switch (ft) {
        case TYPE_BOOL:
            f->type_id     = TYPEID_BOOLEAN;
            f->value_width = 1;
            break;
        case TYPE_INT1:
            f->type_id     = TYPEID_NUMERIC;
            f->value_width = 1;
            break;
        case TYPE_INT2:
            f->type_id     = TYPEID_NUMERIC;
            f->value_width = 2;
            break;
        case TYPE_INT4:
            f->type_id     = TYPEID_NUMERIC;
            f->value_width = 4;
            break;
        case TYPE_INT8:
            f->type_id     = TYPEID_NUMERIC;
            f->value_width = 8;
            break;
        case TYPE_FLOAT4:
            f->type_id     = TYPEID_FLOAT;
            f->value_width = 4;
            break;
        case TYPE_FLOAT8:
            f->type_id     = TYPEID_FLOAT;
            f->value_width = 8;
            break;
        case TYPE_CHAR:
            // Fixed-length byte string. Treated as raw bytes for comparison.
            // max_size must be set before calling this.
            f->type_id     = TYPEID_NUMERIC;    // lexicographic via numeric path
            f->value_width = (uint8_t)f->max_size;
            break;
        case TYPE_VARCHAR:
            f->type_id     = TYPEID_VARCHAR;
            f->value_width = 0;                 // runtime length — not static
            break;
        default:
            assert(false && "unknown FieldType");
    }
}


// ─────────────────────────────────────────────────────────────────────────────
//  Schema
//
//  INVARIANTS (hold after schema_build()):
//    1. fields[i].fixed_offset is correct for all i
//    2. fixed_size == sum of all fields[i].fixed_size
//    3. has_varchar reflects whether any field is TYPE_VARCHAR
//    4. key_field_index refers to a fixed-width field (VARCHAR keys forbidden)
//
//  Populate fields[] and set n_fields + key_field_index, then call
//  schema_build() exactly once before using the schema for anything.
// ─────────────────────────────────────────────────────────────────────────────

#define SCHEMA_MAX_FIELDS 32

typedef struct {
    // ── set by caller ────────────────────────────────────────────────────────
    FieldDef fields[SCHEMA_MAX_FIELDS];
    uint16_t n_fields;
    uint8_t  key_field_index;   // index into fields[]; must be a fixed-width field

    // ── computed by schema_build() ───────────────────────────────────────────
    uint16_t fixed_size;        // total size of the fixed region in bytes
    bool     has_varchar;       // true if any field is TYPE_VARCHAR
} Schema;


// ─────────────────────────────────────────────────────────────────────────────
//  schema_build()
//
//  Call once after populating fields[], n_fields, and key_field_index.
//  Computes fixed_offset, fixed_size, type_id, value_width for every field.
//  Asserts that the key field is not variable-length.
// ─────────────────────────────────────────────────────────────────────────────

static inline void schema_build(Schema *s) {
    assert(s != NULL);
    assert(s->n_fields > 0 && s->n_fields <= SCHEMA_MAX_FIELDS);

    uint16_t offset  = 0;
    bool     has_var = false;

    for (uint16_t i = 0; i < s->n_fields; i++) {
        FieldDef *f = &s->fields[i];

        // Derives type_id + value_width from the caller-supplied type.
        fielddef_set_type(f, f->type);

        f->fixed_size   = field_fixed_size(f->type, f->max_size);
        f->fixed_offset = offset;
        offset         += f->fixed_size;

        if (field_is_variable(f->type)) has_var = true;
    }

    s->fixed_size  = offset;
    s->has_varchar = has_var;

    assert(s->key_field_index < s->n_fields);
    assert(!field_is_variable(s->fields[s->key_field_index].type)
           && "key field must be fixed-width — variable-length keys are not supported");
}


// ─────────────────────────────────────────────────────────────────────────────
//  Field access helpers
// ─────────────────────────────────────────────────────────────────────────────

// Returns a pointer to the key field's FieldDef.
static inline const FieldDef *schema_key_field(const Schema *s) {
    assert(s != NULL);
    return &s->fields[s->key_field_index];
}

// For fixed-width fields: returns a pointer into record_data at the field's
// offset. The pointer is borrowed — do not free it, do not let it outlive
// record_data.
//
// Asserts if called on a VARCHAR field — use schema_varchar_ptr() instead.
static inline uint8_t *schema_field_ptr(const Schema *s,
                                         uint8_t *record_data,
                                         uint8_t  field_idx) {
    assert(s != NULL && record_data != NULL);
    assert(field_idx < s->n_fields);
    assert(!field_is_variable(s->fields[field_idx].type)
           && "use schema_varchar_ptr() for VARCHAR fields");
    return record_data + s->fields[field_idx].fixed_offset;
}

// For VARCHAR fields: returns a pointer to the actual data in the var region,
// and writes the runtime byte length into *out_len (may be NULL if unneeded).
//
// The returned pointer is borrowed — same lifetime rules as schema_field_ptr.
// Asserts if called on a non-VARCHAR field.
static inline uint8_t *schema_varchar_ptr(const Schema *s,
                                           uint8_t  *record_data,
                                           uint8_t   field_idx,
                                           uint16_t *out_len) {
    assert(s != NULL && record_data != NULL);
    assert(field_idx < s->n_fields);
    assert(field_is_variable(s->fields[field_idx].type)
           && "use schema_field_ptr() for fixed-width fields");

    uint8_t *desc_ptr  = record_data + s->fields[field_idx].fixed_offset;
    VarDesc  d         = vardesc_read(desc_ptr);
    uint8_t *var_start = record_data + s->fixed_size;

    if (out_len) *out_len = d.var_len;
    return var_start + d.var_offset;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Record sizing
// ─────────────────────────────────────────────────────────────────────────────

// Compute the total serialized byte size of a record you are about to build.
//
// For schemas without VARCHAR: always returns s->fixed_size.
// For schemas with VARCHAR: pass var_lens[] (one entry per VARCHAR field,
// in field order) and n_var (number of VARCHAR fields).
// Pass var_lens=NULL and n_var=0 if the schema has no VARCHAR fields.
static inline uint32_t schema_record_size(const Schema *s,
                                           const uint16_t *var_lens,
                                           uint16_t        n_var) {
    assert(s != NULL);
    uint32_t size    = s->fixed_size;
    uint16_t var_idx = 0;

    for (uint16_t i = 0; i < s->n_fields; i++) {
        if (field_is_variable(s->fields[i].type)) {
            assert(var_idx < n_var && "not enough var_lens entries");
            size += var_lens[var_idx++];
        }
    }
    return size;
}

// Read the total byte size of an already-serialized record by summing
// the var-region lengths stored in each VARCHAR field's descriptor.
// For fixed-only schemas this is just s->fixed_size.
static inline uint32_t schema_record_size_from_data(const Schema  *s,
                                                      const uint8_t *record_data) {
    assert(s != NULL && record_data != NULL);
    if (!s->has_varchar) return s->fixed_size;

    uint32_t var_total = 0;
    for (uint16_t i = 0; i < s->n_fields; i++) {
        if (field_is_variable(s->fields[i].type)) {
            VarDesc d = vardesc_read(record_data + s->fields[i].fixed_offset);
            var_total += d.var_len;
        }
    }
    return s->fixed_size + var_total;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Example: IoT sensor schema
//
//  Uncomment and call build_sensor_schema() to get a ready-to-use Schema.
//
//  Layout after schema_build():
//    offset  0 : sensor_id    INT4   [4 bytes]
//    offset  4 : timestamp_ms INT8   [8 bytes]  ← key
//    offset 12 : temperature  FLOAT8 [8 bytes]
//    offset 20 : humidity     FLOAT8 [8 bytes]
//    offset 28 : pressure     FLOAT8 [8 bytes]
//    fixed_size = 36 bytes
// ─────────────────────────────────────────────────────────────────────────────

/*
static inline Schema build_sensor_schema(void) {
    Schema s = {0};

    s.fields[0].type = TYPE_INT4;
    strncpy(s.fields[0].name, "sensor_id", 32);

    s.fields[1].type = TYPE_INT8;
    strncpy(s.fields[1].name, "timestamp_ms", 32);

    s.fields[2].type = TYPE_FLOAT8;
    strncpy(s.fields[2].name, "temperature", 32);

    s.fields[3].type = TYPE_FLOAT8;
    strncpy(s.fields[3].name, "humidity", 32);

    s.fields[4].type = TYPE_FLOAT8;
    strncpy(s.fields[4].name, "pressure", 32);

    s.n_fields        = 5;
    s.key_field_index = 1;   // timestamp_ms is the B+ Tree key

    schema_build(&s);
    return s;
}
*/
