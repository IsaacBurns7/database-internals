# RDBMS Build Plan — Post-B+Tree

Clustered-index, IoT/time-series storage engine in C++. Tree, tuple layer, and schema/type system are done.

**Ordering note:** executor before WAL. The executor is what makes recovery testable — with a `SELECT`/`INSERT` path you can crash-fuzz (`SIGKILL` mid-workload, reopen, query, diff against an oracle); without one you're driving recovery through hand-written call sequences and debug dumps. The executor never sees a log record, so nothing is wasted by doing it first. Flip Phases 2 and 3 if you disagree.

---

## Phase 0 — Buffer Pool

### Build

- Frame array + page table (`page_id → frame_id`)
- Pin counts, dirty flags
- LRU-K or clock eviction
- RAII page guards (read/write guard types, even if the "latch" inside is a no-op for now — get the *shape* right so latching later is a fill-in, not a refactor)
- Free list
- `NewPage` / `FetchPage` / `DeletePage` / `FlushAll`

### Refactor

Every disk-manager call site in the tree becomes a guard acquisition. Mechanical, but it touches everything. This is the risky part.

### Exit criteria

- [ ] Randomized differential test vs. `std::map`, 100k+ mixed ops, invariant checker after each
- [ ] Close/reopen mid-test, resume against the same reference map
- [ ] `assert(all pin counts == 0)` after every top-level operation — catches the refactor's real failure mode, one line
- [ ] Pool sized far smaller than the working set, so eviction actually runs during tests. A pool that never evicts is not tested.

### Reading

**Primary — read before building**

| Source | Why |
|---|---|
| AoDS §5, esp. §5.3 (Buffer Management) | ~4 pages. Temporal vs. spatial control, why the DBMS manages its own buffer rather than trusting the OS. |
| Petrov, *Database Internals* Ch. 5 §Buffer Management | The section you were told to pull forward. Page eviction, pinning, the basic frame/page-table structure. |
| CMU 15-445 "Database Storage" + "Buffer Pools" lectures | Pavlo's slides on this are better than either book. Free at 15445.courses.cs.cmu.edu. |
| BusTub Project 1 spec + `buffer_pool_manager.h`, `page_guard.h` | You already reconstruct against BusTub. The guard API is exactly the shape you want; copy the interface, write the body yourself. |

https://github.com/cmu-db/bustub/blob/master/src/include/buffer/buffer_pool_manager.h
https://github.com/cmu-db/bustub/blob/master/src/include/storage/page/page_guard.h

https://15445.courses.cs.cmu.edu/spring2026/project1/

https://15445.courses.cs.cmu.edu/fall2025/slides/03-storage1.pdf
https://15445.courses.cs.cmu.edu/fall2025/notes/03-storage1.pdf
https://15445.courses.cs.cmu.edu/fall2025/slides/04-bufferpool.pdf
https://15445.courses.cs.cmu.edu/fall2025/slides/04-bufferpool.pdf


**Reference while building**

- **PostgreSQL `src/backend/storage/buffer/README`** — genuinely one of the best pieces of prose documentation in any open-source DB. Explains pin/refcount discipline, the buffer access strategy rings, and why `pageLSN`-before-write is enforced here. Read this one carefully; it's the closest thing to a spec for what you're writing.
- **`bufmgr.c` itself** — for how `ReadBuffer`/`ReleaseBuffer` bracket every access. Skim, don't study.

**Depth, if you want it**

- Effelsberg & Härder, *Principles of Database Buffer Management* (TODS 1984) — the origin paper. Replacement policy taxonomy, why DB access patterns break LRU.
- O'Neil, O'Neil & Weikum, *The LRU-K Page Replacement Algorithm for Database Disk Buffering* (SIGMOD 1993) — read this if you implement LRU-K rather than clock. Short, and the correlated-reference problem it solves is exactly what a range-scanning time-series workload creates.
- Crotty, Leis & Pavlo, *Are You Sure You Want to Use MMAP in Your DBMS?* (CIDR 2022) — 6 pages on why you're doing all this work instead of `mmap`. Good interview fodder.

**Later, not now**

- Leis et al., *LeanStore* (ICDE 2018) — pointer swizzling, optimistic latch coupling. Genuinely relevant to a from-scratch engine, but it's a Phase 4 conversation.

---

## Phase 1 — Catalog

### Build

System table: `table_name → (table_id, serialized schema, root_page_id)`.

Bootstrap via a fixed metadata page — extend the one already holding your tree root. The catalog is itself a table, so something has to find it without consulting the catalog; a known page number is the answer.

Load into a `name → TableInfo` map at startup. Executors need lookups to be free.

Your `WriteBuffer`/`ReadBuffer` already handles the schema blob, so the real work is the bootstrap and the in-memory map.

**Defer:** statistics, secondary-index catalog, DDL versioning, `ALTER`.

### Exit criteria

- [ ] Create two tables with different schemas, insert into both, close, reopen, read both back correctly

### Reading

This phase is short on theory and long on "look at how someone else did it." Two pages of reading, then two reference implementations.

| Source | Why |
|---|---|
| AoDS §7.1 (Catalog Manager) | ~2 pages, and that's the whole literature. Says the catalog is stored as tables in the database itself, notes it gets denormalized and cached in memory for exactly the reason you're caching it. |
| **SQLite file format spec §2.6** (sqlite.org/fileformat2.html) | **The single most useful thing here.** `sqlite_schema` lives at page 1 — fixed, always — with columns `type, name, tbl_name, rootpage, sql`. That `rootpage` column is precisely your `root_page_id`, and page-1-by-fiat is precisely your bootstrap. It's the minimal correct design, and it's a design you can read end to end in ten minutes. |
| BusTub `src/include/catalog/catalog.h` | In-memory `unordered_map` of name → `TableInfo`, `TableInfo` holding schema + heap + oid. The shape your executors will want. Note BusTub doesn't persist its catalog — you do, so this is a shape reference, not a completeness reference. |
| PostgreSQL docs, "System Catalogs" (`pg_class`, `pg_attribute`) | Skim only. Useful for seeing what the mature version grows into — one row per relation, one row per column, `relfilenode` pointing at storage. |
| PostgreSQL `src/backend/bootstrap/README` + the `pg_filenode.map` mechanism | Optional, but it's the industrial answer to your exact bootstrap problem: some relations are "nailed," their locations known without a catalog lookup. Confirms you're not hacking — everyone does this. |

**One design decision worth noting:** SQLite stores the schema as the original `CREATE TABLE` *text* and re-parses it on open; you're storing a serialized binary blob. Yours is faster and less flexible. Since you have no SQL front-end yet, yours is also the only option — but be able to articulate the tradeoff, because it's a natural interview follow-up.

---

## Phase 2 — Executor Vertical Slice

Volcano `Init` / `Next` / `Close`, one tuple at a time.

### Order

1. **SeqScan** — for you this is the leaf iterator, since the clustered index is the heap
2. **IndexScan** with range pushdown — `(lo, hi)` into a leaf-chain walk. The one that matters for time-series.
3. **Filter** and **Projection** — trivial once the expression tree exists
4. **Insert** / **Delete** — needed to drive Phase 3
5. **Expression evaluation** — column refs, constants, comparisons, arithmetic, `AND`/`OR`. Recursive `Evaluate(tuple, schema) → Value` over your existing `Value` union.

**Target:** `SELECT ts, temp FROM readings WHERE ts BETWEEN ? AND ? AND temp > ?` end to end.

**Skip the parser.** Hand-build plan trees in the test harness. You've already written a recursive-descent parser for the course scheduler, so the skill is proven and the SQL front-end is pure surface area — add it after recovery works, or never.

**Defer:** joins, aggregation, sorting, `LIMIT`, the optimizer entirely.

### Reading

- Graefe, *Volcano — An Extensible and Parallel Query Evaluation System* — one sitting, ~15 pages, before you start. The open/next/close protocol *is* the idea.
- AoDS §4 — alongside, while writing.
- BusTub `AbstractExecutor` — shape reference.

---

## Phase 3 — WAL + Recovery

Single-threaded, redo-only, no undo.

### Build

- Log record format (LSN, type, `page_id`, payload)
- Append-only log file with a tail buffer
- `pageLSN` in every page header
- **The WAL protocol hook in eviction** — force log through `pageLSN` before any page write. That hook is the whole reason this phase follows Phase 0.

### Logging strategy, given the clustered design

Physiological redo for ordinary leaf inserts/deletes; **full-page images for splits and merges.** Space-hungry and coarse, but redo stays idempotent and you skip structural undo entirely — which is the thing you can't dodge the usual way, since with a clustered index the tree *is* the heap and there's no "rebuild the secondary index during recovery" escape hatch.

### Recovery

Analysis (scan log, build dirty page table) → redo (replay from the DPT's minimum `recLSN`, apply only where `pageLSN < record.LSN`). No undo phase.

Skip checkpointing until log scan time gets annoying, then add fuzzy checkpoints.

### Exit criteria

- [ ] Crash-injection harness: run a workload through the executor, `SIGKILL` at a randomized point, reopen, replay expected state from an oracle log, `SELECT` and diff
- [ ] Loop it a few thousand times, different crash points and seeds
- [ ] Torn-page handling decided — checksums per page at minimum, so you can *detect* one

### Reading

AoDS §6.3–6.5 first — a few pages summarizing ARIES, makes the real paper go much faster.

Then ARIES **by content, not section number**:

- Introduction — latches vs. locks, steal/no-force buffer management, LSN / `pageLSN` / the WAL protocol
- Data structures — log record format, page format, and the **dirty page table** (the DPT is what lets analysis compute where redo starts; the transaction table you can note and ignore)
- Normal processing — how updates stamp `pageLSN`, fuzzy checkpointing
- Restart processing — analysis and redo. Redo is the core insight: repeating history, with `pageLSN >= record.LSN` as the idempotence test.

**Skip on this pass:** CLRs, rollback, savepoints, nested top actions, fine-granularity locking, media recovery.

---

## Phase 4 — Concurrency

Latch crabbing in the tree, lock manager, isolation levels, undo/CLRs, the rest of ARIES.

One design problem in three places — that's why it's one phase and why it's last. Petrov's concurrency sections and the deferred half of ARIES both become relevant here. Graefe, *Modern B-Tree Techniques* is the real reference for the tree side.

---

## Running Throughout

One test harness that owns the oracle — `std::map` early, then a reference table + expected-results file once the executor exists. Every phase adds cases to it rather than getting its own bespoke test file. The Phase 3 crash-fuzzer is only cheap to write because Phases 0–2 already built the driver.

---

## Two Things Worth Pulling Forward

**Right-only append fastpath** (Phase 0 or 1) — monotonic timestamps hit the rightmost leaf on essentially every insert. Caching it, skipping the root-to-leaf descent, and splitting asymmetrically instead of 50/50 is ~40 lines and materially changes your insert profile. Cheap now, and it's what makes the design legibly *time-series* rather than generic. Petrov covers it under right-only appends; Postgres calls it the fastpath in `_bt_search`.

**Page checksums** (Phase 0) — a `uint32` in the header, verified on read. Free now, and Phase 3 needs it to distinguish a torn page from a corrupt one.

---

## Resume Milestone

End of Phase 3. *"Crash-consistent storage engine with WAL recovery, verified by randomized crash injection"* is a substantially stronger line than anything Phase 4 adds, and it's the version you can talk through for twenty minutes without hitting a part you hand-waved.
