// value.h

#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
#include <stdexcept>

// ─────────────────────────────────────────────
//  TypeId
//  Numeric covers all integer widths.
//  Float covers float + double.
// ─────────────────────────────────────────────

enum class TypeId : uint8_t {
    INVALID = 0,
    BOOLEAN,
    NUMERIC,    // int8, int16, int32, int64 — distinguished by width
    FLOAT,      // float or double — distinguished by width
    VARCHAR,
};

// ─────────────────────────────────────────────
//  Value
//  A tagged union. Width disambiguates within NUMERIC and FLOAT.
// ─────────────────────────────────────────────

struct Value {
    TypeId  type_id;
    uint8_t width;      // bytes: 1,2,4,8 for NUMERIC; 4,8 for FLOAT; 0 otherwise

    union {
        int64_t  integer;   // all NUMERIC values sign-extend into here
        double   fp;        // all FLOAT values widen into here
        bool     boolean;
        struct {
            char    *data;
            uint16_t len;
        } varchar;
    } val;

    // ── constructors ──────────────────────────

    static Value make_int(int64_t v, uint8_t width = 8) {
        assert(width == 1 || width == 2 || width == 4 || width == 8);
        Value r; r.type_id = TypeId::NUMERIC; r.width = width; r.val.integer = v;
        return r;
    }
    static Value make_float(double v, uint8_t width = 8) {
        assert(width == 4 || width == 8);
        Value r; r.type_id = TypeId::FLOAT; r.width = width; r.val.fp = v;
        return r;
    }
    static Value make_bool(bool v) {
        Value r; r.type_id = TypeId::BOOLEAN; r.width = 1; r.val.boolean = v;
        return r;
    }
    // varchar does NOT own the data — caller manages lifetime
    static Value make_varchar(const char *data, uint16_t len) {
        Value r; r.type_id = TypeId::VARCHAR; r.width = 0;
        r.val.varchar.data = const_cast<char*>(data);
        r.val.varchar.len  = len;
        return r;
    }
};
