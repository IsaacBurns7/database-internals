The B+ Tree only ever touches three things:

Key — compare, copy, free, serialize, deserialize. That's its entire interface with the type system.
Raw uint8_t* for records — the tree stores and returns them but never interprets them. It only needs to know the byte length.
Page buffers — borrowed pointers. Never store a raw page pointer across a function boundary. key_copy before anything leaves the page.

What the tree is not allowed to touch: Value, Schema, Tuple, Type, FieldDef. If you find yourself reaching for any of those inside a tree function, you're in the wrong layer.
The one invariant to encode in your skeleton right now: every leaf slot is [key_serialized_size: 2B][key_data][record_size: 4B][record_data]. Get that slot layout comment into your leaf node code before you write a single real function, because split and scan both depend on being able to walk slots, and changing this layout later means rewriting both.
