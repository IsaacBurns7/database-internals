# Intent

This project appears to be building a small database engine where the `type/` layer is the runtime contract between logical SQL values and physical storage layout.

The current intent of the type system is:

- represent typed values in a compact tagged runtime container (`Value`)
- centralize type-specific behavior behind `Type`
- support comparison, arithmetic where it makes sense, and serialization/deserialization
- keep one singleton implementation per concrete `TypeId` so callers can dispatch by runtime type

The concrete types currently suggest a minimal, pragmatic model:

- `BOOLEAN` stores as 1 byte and only needs comparison plus round-trip serialization
- `NUMERIC` stores all integer widths through one code path, using `width` to preserve size and sign behavior
- `FLOAT` similarly uses `width` to distinguish `float` and `double`
- `VARCHAR` is treated as length-prefixed inline data for now, with the intent that the length prefix is part of the fixed tuple layout while the payload may later move to an arena or overflow storage

How this fits the rest of the database design:

- `column.h` and `schema.h` are the bridge from logical table definitions to record layout
- `tuple.h` describes the intended record encoding: null bitmap, fixed section, then variable-length tail
- `Type` is the low-level service used by schema/tuple code to compare values, compute sizes, and serialize fields into page-friendly bytes
- `storage/` owns pages, records, and disk I/O, so the type layer should stay focused on per-value encoding and decoding rather than page management

The design direction looks intentionally incremental:

- start with a small set of built-in types
- keep width as runtime metadata instead of splitting every integer and float width into separate classes
- preserve enough structure to later grow into richer VARCHAR handling, nullability, schemas, and query execution

This means the type layer is not the whole database, but the foundation that lets schema, tuple, and storage code agree on exact byte layout.