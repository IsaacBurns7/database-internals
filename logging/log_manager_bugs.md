# log_manager bugs

Found while reviewing `log_manager.cpp` / `log_manager.h` (before the `append()` refactor).

## Fixed by the `append()` refactor

1. **Capacity check is always true** — `log_update` and `log_txn_begin` did
   `if (append_buffer_fill_offset + sizeof(lrh) ...)` with no comparison, so the
   condition is nonzero and every call returned `INVALID_LSN`.
   Fix: `if (append_buffer_fill_offset + lrh.size > APPEND_BUFFER_SIZE)`.

2. **Capacity check outside the lock in `log_update`** — the check ran before
   `scoped_lock` was taken, so two threads could both pass it and overrun the buffer.
   Fix: check and reserve under `mtx`.

3. **CRC truncated to 1 byte in `log_update`** — the result of `crc32c::compute` was
   cast to `std::byte` and written to a single byte at `offsetof(LogRecordHeader, crc)`.
   Fix: compute the CRC over the full record (header + images, with `crc == 0`) and
   `memcpy` all 4 bytes into the crc field.

4. **`log_txn_commit` has no `return`** — undefined behavior for a non-void function.

5. **COMMIT has no `prev_lsn`** — `log_txn_commit(txid_t)` had no way to link to the
   txn's last record, breaking the undo backchain. Fix: take `prev_lsn` as a parameter.

6. **`undo_next_lsn` left as 0 on non-CLR records** — the header comment says it should be
   `INVALID_LSN` otherwise, but `LogRecordHeader{}` zeroes it, and 0 may be a valid LSN.

## Still open

7. **`APPEND_BUFFER_SIZE` / `FILL_BUFFER_SIZE` are 512 MiB, not 64 MiB** —
   `(64 << 3) << 20` is 512 MiB, and the macro isn't wrapped in outer parentheses
   (so e.g. `x / APPEND_BUFFER_SIZE` expands to `x / (64 << 3) << 20`). Both buffers are
   inline member arrays, so each `LogManager` is ~1 GiB.
   Fix: `static constexpr size_t APPEND_BUFFER_SIZE = 64 << 20;` (and consider heap-allocating).

8. **`log_txn_abort` can't be a plain log call** — abort has to walk the undo chain,
   apply undo, and write a CLR per update before END. That rollback logic (the reason it
   takes a `BufferPoolManager&`) likely belongs in a TransactionManager / RecoveryManager,
   with LogManager only exposing the ABORT / CLR / END record writers.

9. **`currentLSN`, `append_buffer_fill_offset`, etc. are never initialized** — the
   constructor is empty.
